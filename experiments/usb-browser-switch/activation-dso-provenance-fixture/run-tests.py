#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New fixed authored DSO evidence only. Preparation/build/run are separately resumable."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused")
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PARENT = HERE.parent
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
PLAN = PARENT / "activation-dso-provenance-plan/provenance-plan.json"
PLAN_SHA = "f627abddb34274af7d04d7b522ab9763bbc3c93f49a605757938c7f17646c75b"
PLAN_README_SHA = "3141ad86a355f71fa8a58625d090d1ba2fa3aa01c5f26bc18f5402c8111dabaf"
SOURCE_NAMES = ("run-tests.py", "compose-source.py", "core.c", "core.h", "loader.c", "loader.h",
                "dp_fixture.h", "dsomod.c", "dso_call.S", "dep.c", "rootstub.c", "test_dso.c", "test-contract.json")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-pthread"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {"ra-host-substitute":"DP_CONTROL_RA_HOST_SUBSTITUTE",
    "omit-entry-owner-veto":"DP_CONTROL_ENTRY_OWNER",
    "ignore-consumed-query-failure":"DP_CONTROL_QUERY_CONSUMPTION",
    "omit-constructor-marker":"DP_CONTROL_CONSTRUCTOR_MARKER",
    "omit-authoritative-handle-record":"DP_CONTROL_HANDLE_RECORD"}
SYMBOLS = {
 "libdp_a.so": {"az_mod_v1_entry":2,"dp_init":2,"dp_foreign_claim":2,"dso_claim_call":2,
    "dp_call_marker":2,"dp_late_claim":2,"dp_mod_id":1,"dp_mod_tag":1,"dp_mod_tags":1,"dp_mod_descriptor":1,
    "dp_bad_descriptor":1,"dp_constructor_count":1},
 "libdp_b.so": {"az_mod_v1_entry":2,"dp_init":2,"dp_foreign_claim":2,"dso_claim_call":2,
    "dp_call_marker":2,"dp_late_claim":2,"dp_mod_id":1,"dp_mod_tag":1,"dp_mod_tags":1,"dp_mod_descriptor":1,
    "dp_bad_descriptor":1,"dp_constructor_count":1},
 "libdp_root.so": {"dp_root_marker":1},
 "libdp_dep.so": {"az_mod_v1_entry":2,"dp_dep_force":2,"dp_dep_invoked":1,"dp_dep_constructor_count":1}}
# Distinct exported data names avoid authored cross-DSO ASan ODR ambiguity.
for _artifact,_prefix in (("libdp_a.so","dp_a_"),("libdp_b.so","dp_b_")):
    SYMBOLS[_artifact] = {(_prefix+n[3:] if t==1 else n):t for n,t in SYMBOLS[_artifact].items()}
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault|FIXTURE_ABORT|warning:|error:",re.I)
SAFE_NAME = re.compile(r"[a-z][a-z0-9-]{0,159}\Z")
MAX_REPORT = 8 * 1024 * 1024
class Refusal(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise Refusal(message)

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def metadata(raw):
    return {"bytes": len(raw), "sha256": digest(raw)}

def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()

def save(path, value):
    with path.open("xb") as out:
        out.write(encoded(value))

def no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, "duplicate JSON key")
        out[key] = value
    return out

def decode(raw):
    return json.loads(raw, object_pairs_hook=no_duplicates,
                      parse_constant=lambda _: (_ for _ in ()).throw(Refusal("nonfinite JSON")))

def regular(path):
    require(path.is_file() and not path.is_symlink(), "regular file required: " + path.name)
    return path.read_bytes()

def inventory(root):
    files, directories = {}, []
    if not root.exists():
        return {"files": files, "directories": directories}
    for p in sorted(root.rglob("*")):
        require(not p.is_symlink(), "symlink in retained packet")
        name = p.relative_to(root).as_posix()
        if p.is_file():
            files[name] = metadata(p.read_bytes())
        elif p.is_dir():
            directories.append(name)
        else:
            raise Refusal("nonregular retained entry: " + name)
    return {"files": files, "directories": directories}

class Elf:
    """Direct ELF64 header/section/symbol inspection only, never execution."""
    def __init__(self, raw, kind):
        require(len(raw) >= 64 and raw[:7] == b"\x7fELF\x02\x01\x01", "ELF64 LE identity")
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", raw)
        require(h[1] == kind and h[2] == 183 and h[3] == 1, "AArch64 ELF type")
        off, width, count, names_index = h[6], h[11], h[12], h[13]
        require(width == 64 and 0 < count < 65535 and names_index < count
                and off + width * count <= len(raw), "complete ordinary ELF section table")
        sections = []
        for i in range(count):
            row = struct.unpack_from("<IIQQQQIIQQ", raw, off + i * width)
            section = dict(zip(("name_offset", "type", "flags", "address", "offset", "size",
                                "link", "info", "alignment", "entry_size"), row))
            require(section["type"] == 8 or section["offset"] + section["size"] <= len(raw),
                    "section extent")
            section["data"] = b"" if section["type"] == 8 else raw[
                section["offset"]:section["offset"] + section["size"]]
            sections.append(section)
        names = sections[names_index]["data"]
        for section in sections:
            section["name"] = self.string(names, section["name_offset"])
        self.sections = sections
        self.symbols = []
        tables = 0
        for section in sections:
            if section["type"] != 2:
                continue
            tables += 1
            require(section["entry_size"] == 24 and section["size"] % 24 == 0
                    and section["link"] < count, "symbol table bounds")
            strings = sections[section["link"]]["data"]
            for at in range(0, section["size"], 24):
                name, info, other, index, address, size = struct.unpack_from("<IBBHQQ", section["data"], at)
                self.symbols.append({"name": self.string(strings, name), "bind": info >> 4,
                                     "type": info & 15, "other": other, "section": index,
                                     "value": address, "size": size})
        require(tables == 1, "one regular symbol table")
        self.kind = kind
        self.raw = raw
        self.phdrs = []
        phoff, phwidth, phcount = h[5], h[9], h[10]
        require((kind == 1 and phcount == 0) or (phwidth == 56 and 0 < phcount < 128 and phoff + phwidth * phcount <= len(raw)), "bounded program headers")
        for i in range(phcount):
            t, f, o, v, pa, fs, ms, al = struct.unpack_from("<IIQQQQQQ", raw, phoff + i * phwidth)
            require(o + fs <= len(raw) and v + ms < (1 << 64) and fs <= ms, "checked program header extents")
            self.phdrs.append(dict(type=t, flags=f, vaddr=v, memsz=ms, filesz=fs, offset=o, align=al))

    @staticmethod
    def string(data, offset):
        require(offset < len(data), "ELF string offset")
        end = data.find(b"\0", offset)
        require(end >= offset, "ELF string terminator")
        return data[offset:end].decode("utf-8", "strict")

    def summary(self):
        return {"machine": "AArch64", "elf_class": 64, "little_endian": True,
                "type": self.kind, "section_count": len(self.sections),
                "symbols": self.symbols, "phdrs": self.phdrs}

def original_symbol(elf):
    rows = [s for s in elf.symbols if s["name"] == "rc_originals"]
    require(len(rows) == 1, "one permanent original array symbol")
    slot = rows[0]
    require(slot["bind"] == 1 and slot["type"] == 1 and slot["size"] == 32
            and slot["value"] % 8 == 0 and 0 < slot["section"] < len(elf.sections),
            "strong defined aligned four-pointer original array")
    section = elf.sections[slot["section"]]
    require(section["type"] == 8 and section["alignment"] >= 8
            and section["flags"] & 3 == 3 and section["address"] <= slot["value"]
            and slot["value"] + 32 <= section["address"] + section["size"],
            "originals retained in allocated writable BSS, not COMMON")
    return slot


def contract_from(raw):
    value=decode(raw)
    require(type(value) is dict and set(value)=={"schema","scope","selectors","controls","limitations"} and value["schema"]==1,"closed contract")
    require(type(value["selectors"]) is dict and len(value["selectors"])==8 and type(value["controls"]) is dict and set(value["controls"])==set(CONTROL_MACROS),"eight selectors/five controls")
    for name, checks in value["selectors"].items():
        require(SAFE_NAME.fullmatch(name) and type(checks) is list and checks and len(checks)==len(set(checks)) and all(SAFE_NAME.fullmatch(x) for x in checks),"unique checks")
    for name,c in value["controls"].items():
        require(type(c) is dict and set(c)=={"macro","selector","expected_failures","safety"},"control shape")
        require(c["macro"]==CONTROL_MACROS[name],"exact control macro")
        require(c["selector"] in value["selectors"] and c["expected_failures"] and len(c["expected_failures"])==len(set(c["expected_failures"])) and all(x in value["selectors"][c["selector"]] for x in c["expected_failures"]),"exact safe failures")
    return value

def select(raw, all_names, label):
    values=list(all_names) if raw is None else raw.split(",")
    require(values and len(values)==len(set(values)) and all(x in all_names for x in values),"known unique "+label)
    return values

def symbol(elf, name, kind=None):
    rows=[s for s in elf.symbols if s["name"]==name and 0<s["section"]<len(elf.sections)]
    require(len(rows)==1,"one defined symbol: "+name)
    s=rows[0]
    require(s["bind"]==1 and (kind is None or s["type"]==kind) and s["type"] in (1,2) and s["size"]>0,"ordinary global typed symbol: "+name)
    return s

def bytes_at(elf, address, size):
    rows=[s for s in elf.sections if s["type"]!=8 and s["flags"] & 6 == 6 and s["address"]<=address and address+size<=s["address"]+s["size"]]
    require(len(rows)==1,"one complete ELF byte extent")
    row=rows[0]; at=address-row["address"]
    return row["data"][at:at+size]

class Run:
    def __init__(self,args):
        self.args=args
        self.path=HERE/"private"/(args.packet or ("run-"+uuid.uuid4().hex))
        require(self.path.parent==HERE/"private" and re.fullmatch(r"run-[0-9a-f]{32}",self.path.name),"owned packet name")
        self.inputs=self.path/"inputs"; self.generated=self.path/"generated"
        self.work=self.path/"work"; self.logs=self.path/"commands"
        self.commands=[]; self.manifest={}; self.attempts=[]; self.contract=None
        self.results={"builds":{},"candidate":{},"controls":{}}
        self.phase="preparation"; self.run_id=uuid.uuid4().hex
        self.docker_path=str(Path(args.docker).resolve())
        self.base_environment={"PATH":"/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin","LANG":"C","LC_ALL":"C","TMPDIR":str(self.path/"tmp"),"PYTHONDONTWRITEBYTECODE":"1"}
        for key in ("HOME","DOCKER_CONFIG","DOCKER_CONTEXT","DOCKER_HOST","DOCKER_TLS_VERIFY","DOCKER_CERT_PATH"):
            if key in os.environ: self.base_environment[key]=os.environ[key]

    def put(self,name,raw,role):
        p=self.inputs/name; p.parent.mkdir(parents=True,exist_ok=True)
        with p.open("xb") as f: f.write(raw)
        self.manifest[name]=dict(metadata(raw),role=role)

    def check_inputs(self):
        require(inventory(self.inputs)["files"]=={n:{k:v[k] for k in ("bytes","sha256")} for n,v in self.manifest.items()},"immutable input closure")
        require(regular(HERE/"run-tests.py")==regular(self.inputs/"new/run-tests.py"),"continuation runner source unchanged")

    def prepare(self):
        require(not self.path.exists(),"fresh packet")
        self.path.mkdir(parents=True)
        for p in (self.inputs,self.work,self.logs,self.path/"tmp"): p.mkdir()
        save(self.path/"started.json",{"schema":1,"image":IMAGE_ID,"runtime_gate":"CLOSED","python":sys.version})
        lockraw=regular(HERE/"review-lock.json"); lock=decode(lockraw)
        require(type(lock) is dict and set(lock)=={"schema","status","files"} and lock["schema"]==1 and lock["status"]=="reviewed-before-build","review lock")
        require(set(lock["files"])==set(SOURCE_NAMES)|{"source-ledger.json"},"closed reviewed source inventory")
        self.put("review-lock.json",lockraw,"reviewed source fence")
        for name,pin in lock["files"].items():
            raw=regular(HERE/name)
            require(metadata(raw)==pin,"reviewed exact source: "+name)
            self.put("new/"+name,raw,"new authored DSO fixture")
        planraw=regular(PLAN); require(digest(planraw)==PLAN_SHA,"frozen plan pin"); plan=decode(planraw)
        self.put("plan/provenance-plan.json",planraw,"frozen reviewed plan")
        planreadme=regular(PLAN.parent/"README.md"); require(digest(planreadme)==PLAN_README_SHA,"frozen plan README pin")
        self.put("plan/README.md",planreadme,"frozen reviewed plan")
        for row in plan["sources"]:
            raw=regular(REPO/row["path"])
            require(metadata(raw)=={k:row[k] for k in ("bytes","sha256")},"reused plan source pin: "+row["name"])
            self.put("frozen/"+row["name"],raw,"reused retained source only")
        save(self.path/"input-manifest.json",self.manifest)
        self.contract=contract_from(regular(self.inputs/"new/test-contract.json"))
        for name in ("run-tests.py","compose-source.py"): ast.parse(regular(self.inputs/"new"/name),filename=name)
        self.check_inputs()
        self.phase="compose"
        # Only the separately authored byte copier/ledger checker runs here.
        # The parent composers, compiler and all runtime selectors remain absent.
        output=self.clean("compose-source",[sys.executable,str(self.inputs/"new/compose-source.py"),"--inputs",str(self.inputs),"--output",str(self.generated)],host=True)
        ledger=decode(regular(self.generated/"composition-ledger.json"))
        require(ledger["runtime_gate"]=="CLOSED","generated ledger closed gate")
        save(self.path/"source-preparation.json",{"schema":1,"composition_stdout":metadata(output),"generated":inventory(self.generated),"review_lock":metadata(lockraw),"old_composer_replayed":False})
        self.results["prepared"]=True
        self.check_inputs()

    def resume(self):
        require(self.path.is_dir() and not self.path.is_symlink(),"existing owned packet")
        state=decode(regular(self.path/"state.json"))
        require(state["schema"]==1 and state["packet"]==self.path.name,"state identity")
        self.manifest=decode(regular(self.path/"input-manifest.json"))
        self.results=state["results"]; self.commands=state["commands"]; self.attempts=state["attempts"]
        self.contract=contract_from(regular(self.inputs/"new/test-contract.json"))
        self.check_inputs()
        prep=decode(regular(self.path/"source-preparation.json"))
        require(inventory(self.generated)==prep["generated"],"frozen generated source closure")
        # Existing build products are immutable; output reports and command logs are append-only.
        for build in self.results.get("builds",{}).values():
            for p,m in build.get("artifacts",{}).items():
                require(metadata(regular(self.work/p))==m,"retained build artifact: "+p)

    def snapshot(self):
        value={"schema":1,"packet":self.path.name,"phase":self.phase,"results":self.results,"commands":self.commands,"attempts":self.attempts}
        raw=encoded(value)
        (self.path/"state.json").write_bytes(raw)
        save(self.path/("state-"+self.run_id+".json"),value)
    def command(self, name, argv, *, container=None, timeout=45):
        require(SAFE_NAME.fullmatch(name) and type(timeout) is int and 0 < timeout < 60,
                "bounded named command")
        require(name not in {c["name"] for c in self.commands}, "command replay refused: " + name)
        relative = "%03d-%s" % (len(self.commands) + 1, name)
        folder = self.logs / relative
        folder.mkdir()
        spec = {"argv": [str(a) for a in argv], "cwd": str(self.path),
                "environment": self.base_environment, "timeout_seconds": timeout,
                "container": container}
        save(folder / "command.json", spec)
        proc, output, error, exception, cleanup = None, b"", b"", None, {}
        try:
            proc = subprocess.Popen(spec["argv"], cwd=self.path, env=self.base_environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    start_new_session=True)
            output, error = proc.communicate(timeout=timeout)
        except BaseException as exc:
            exception = type(exc).__name__ + ": " + str(exc)
            if isinstance(exc, subprocess.TimeoutExpired):
                output, error = exc.output or b"", exc.stderr or b""
            try:
                (folder / "interrupted-stdout.bin").write_bytes(output)
                (folder / "interrupted-stderr.bin").write_bytes(error)
            except BaseException as capture_error:
                cleanup["prefix_capture_error"] = repr(capture_error)
            finally:
                if proc is not None and proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                        cleanup["process_group_kill_requested"] = True
                    except ProcessLookupError:
                        cleanup["process_group_already_gone"] = True
                    except BaseException as kill_error:
                        cleanup["process_group_kill_error"] = repr(kill_error)
                if container is not None:
                    cleanup_argv = [self.docker_path, "rm", "-f", container]
                    cleanup["container_argv"] = cleanup_argv
                    try:
                        cr = subprocess.run(cleanup_argv, env=self.base_environment, cwd=self.path,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            timeout=15, check=False)
                        (folder / "cleanup-stdout.bin").write_bytes(cr.stdout)
                        (folder / "cleanup-stderr.bin").write_bytes(cr.stderr)
                        cleanup.update(returncode=cr.returncode, stdout=metadata(cr.stdout),
                                       stderr=metadata(cr.stderr))
                    except BaseException as cleanup_error:
                        cleanup["container_error"] = repr(cleanup_error)
                        if isinstance(cleanup_error, subprocess.TimeoutExpired):
                            (folder / "cleanup-interrupted-stdout.bin").write_bytes(cleanup_error.output or b"")
                            (folder / "cleanup-interrupted-stderr.bin").write_bytes(cleanup_error.stderr or b"")
                if proc is not None:
                    try:
                        drained_out, drained_err = proc.communicate(timeout=5)
                        output, error = drained_out, drained_err
                    except BaseException as drain_error:
                        cleanup["drain_error"] = repr(drain_error)
        finally:
            (folder / "stdout.bin").write_bytes(output)
            (folder / "stderr.bin").write_bytes(error)
            receipt = dict(spec, name=name, directory=relative,
                           returncode=None if proc is None else proc.returncode,
                           exception=exception, cleanup=cleanup,
                           stdout=metadata(output), stderr=metadata(error))
            save(folder / "receipt.json", receipt)
            self.commands.append(dict(receipt, receipt_sha256=digest(encoded(receipt))))
        require(exception is None, "command exception: " + name + ": " + str(exception))
        return receipt["returncode"], output, error

    def docker(self, name, argv, extra_environment=None, runtime_build=None, evidence=None):
        container = "az-dso-provenance-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
        env = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
               "LANG": "C", "LC_ALL": "C", **(extra_environment or {})}
        command = [self.docker_path, "run", "--name", container, "--rm", "--pull=never",
                   "--network=none", "--platform=linux/arm64", "--read-only",
                   "--tmpfs", "/tmp:rw,nosuid,nodev", "--workdir", "/out",
                   "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                   "--mount", "type=bind,source=" + str(self.generated) + ",target=/gen,readonly",
                   "--mount", "type=bind,source=" + str(self.work) + ",target=/out" + (",readonly" if runtime_build is not None else "")]
        if runtime_build is not None:
            command += ["--mount", "type=bind,source=" + str(runtime_build) + ",target=/build,readonly",
                        "--mount", "type=bind,source=" + str(evidence) + ",target=/evidence"]
        command += [IMAGE_ID, "env", "-i"]
        command += [k + "=" + v for k, v in env.items()] + list(argv)
        return self.command(name, command, container=container)

    def clean(self, name, argv, *, host=False, empty_stdout=False):
        rc, out, err = self.command(name, argv) if host else self.docker(name, argv)
        require(rc == 0 and err == b"" and not DIAGNOSTIC.search(out.decode("utf-8", "replace")),
                "clean successful command: " + name)
        if empty_stdout:
            require(out == b"", "empty compile/link stdout: " + name)
        return out

    def inspect_image(self):
        raw = self.clean("image-inspect", [self.docker_path, "image", "inspect", IMAGE_ID], host=True)
        parsed = decode(raw)
        require(type(parsed) is list and len(parsed) == 1 and type(parsed[0]) is dict,
                "one Docker image")
        info = parsed[0]
        require(info.get("Id") == IMAGE_ID and info.get("Architecture") == "arm64"
                and info.get("Os") == "linux", "pinned Linux ARM64 image")
        return {key: info[key] for key in ("Id", "Architecture", "Os")}


    def flags(self, mode, host=False, macro=None):
        flags=COMMON+(["-fno-pie","-DDP_FIXTURE=1"] if host else ["-fPIC"])
        if mode=="asan-ubsan": flags+=SAN_FLAGS
        if macro: flags += ["-D"+macro+"=1"]
        return flags

    def elf_manifest(self, folder):
        manifests=[]; header=["/* Generated from retained ELF bytes before host compilation. */", "#ifndef DP_EXPECTED_SYMBOLS_H", "#define DP_EXPECTED_SYMBOLS_H"]
        for index,(name,names) in enumerate(SYMBOLS.items()):
            raw=regular(folder/name); elf=Elf(raw,3)
            require(any(p["type"]==2 for p in elf.phdrs),"one DSO dynamic metadata presence")
            symbols=[symbol(elf,n,t) for n,t in names.items()]
            for selected in symbols:
                need=1 if selected["type"]==2 else 4
                require(any(p["type"]==1 and p["flags"]&need==need and p["vaddr"]<=selected["value"] and selected["value"]+selected["size"]<=p["vaddr"]+p["memsz"] for p in elf.phdrs),"selected symbol complete segment coverage")
            if index<2:
                call=symbol(elf,"dso_claim_call",2); marker=symbol(elf,"dp_call_marker",2)
                require(marker["size"]==4 and call["value"]<marker["value"] and marker["value"]+4<=call["value"]+call["size"],"post-BLR marker within ordinary call stub")
                instruction=struct.unpack("<I",bytes_at(elf,marker["value"]-4,4))[0]
                require(instruction & 0xfffffc1f==0xd63f0000,"exact immediately preceding BLR")
            header += ["static const dp_elf_phdr dp_phdr_%d[] = {"%index]
            for h in elf.phdrs:
                header += ["{%du,%du,UINT64_C(%d),UINT64_C(%d),UINT64_C(%d),UINT64_C(%d),UINT64_C(%d)},"%(h["type"],h["flags"],h["vaddr"],h["memsz"],h["filesz"],h["offset"],h["align"])]
            header += ["};","static const dp_elf_symbol dp_sym_%d[] = {"%index]
            for x in symbols: header += ['{"%s",UINT64_C(%d),UINT64_C(%d),%du},'%(x["name"],x["value"],x["size"],x["type"])]
            header += ["};"]
            manifests.append(dict(file=name,**metadata(raw),phdrs=elf.phdrs,symbols=symbols))
        header += ["static const dp_elf_manifest dp_expected[4] = {"]
        for i,m in enumerate(manifests): header += ['{"%s",dp_phdr_%d,%du,dp_sym_%d,%du},'%(m["file"],i,len(m["phdrs"]),i,len(m["symbols"]))]
        header += ["};","#endif",""]
        (folder/"expected-symbols.h").write_text("\n".join(header))
        save(folder/"elf-manifest.json",{"schema":1,"artifacts":manifests,"source":"actual ELF64 bytes","ordinary_blr_marker_checked":True,"selected_ifunc_allowed":False})
        return manifests

    def build_dsos(self,mode):
        key="dsos-"+mode
        require(key not in self.results["builds"],"successful DSO build replay refused")
        folder=self.work/"build"/"dsos"/mode; folder.mkdir(parents=True)
        target="/out/"+folder.relative_to(self.work).as_posix(); flags=self.flags(mode)
        for name,index in (("a",0),("b",1)):
            self.clean("compile-dso-"+name+"-"+mode,["gcc",*flags,"-DDP_INDEX="+str(index),"-I/src/new","-I/gen","-c","/src/new/dsomod.c","-o",target+"/dso-"+name+".o"],empty_stdout=True)
        self.clean("compile-dso-call-"+mode,["gcc","-g","-fPIC","-c","/src/new/dso_call.S","-o",target+"/dso_call.o"],empty_stdout=True)
        for name in ("dep","rootstub"):
            self.clean("compile-dso-"+name+"-"+mode,["gcc",*flags,"-I/src/new","-I/gen","-c","/src/new/"+name+".c","-o",target+"/"+name+".o"],empty_stdout=True)
        for name in ("a","b"):
            self.clean("link-dso-"+name+"-"+mode,["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_"+name+".so",target+"/dso-"+name+".o",target+"/dso_call.o","-o",target+"/libdp_"+name+".so"],empty_stdout=True)
        self.clean("link-dso-dep-"+mode,["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_dep.so",target+"/dep.o","-o",target+"/libdp_dep.so"],empty_stdout=True)
        self.clean("link-dso-root-"+mode,["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_root.so",target+"/rootstub.o","-L"+target,"-ldp_dep","-Wl,-rpath,$ORIGIN","-o",target+"/libdp_root.so"],empty_stdout=True)
        self.finish_dsos(mode,folder,target)

    def finish_dsos(self,mode,folder,target):
        key="dsos-"+mode
        self.elf_manifest(folder)
        for name in SYMBOLS:
            label=name.replace("_","-").replace(".so","")
            for kind,argv in (("elf",["readelf","-W","-h","-l","-s","-d",target+"/"+name]),("disassembly",["objdump","-d",target+"/"+name])):
                raw=self.clean(mode+"-"+label+"-"+kind,argv)
                (folder/(name+"."+kind+".txt")).write_bytes(raw)
        root_dynamic=regular(folder/"libdp_root.so.elf.txt")
        require(b"Shared library: [libdp_dep.so]" in root_dynamic and b"$ORIGIN" in root_dynamic,"declared dependency and ORIGIN retained")
        artifacts={p.relative_to(self.work).as_posix():metadata(regular(p)) for p in folder.iterdir() if p.is_file()}
        self.results["builds"][key]={"kind":"dso","mode":mode,"artifacts":artifacts,"typed_symbols_and_phdrs":True,"actual_blr_marker":True}

    def adopt_plain_dsos(self):
        """One reviewed oracle-only failure packet; never a general cache importer."""
        name="run-8cd5c320082b4b71ba5139f6c0e388e8"
        require(self.args.reuse_plain_dso_packet==name and not self.results["builds"],"one scoped pre-host adoption")
        old=HERE/"private"/name
        result_name="result-3aa39420a18b49f1ac5ef225638ecec1.json"
        raw=regular(old/result_name)
        require(digest(raw)=="1afcfceb1be7a7f343a7fb2d8ef63e28db51ddd3f3b05ba5727a142646217e5b","reviewed failed result pin")
        result=decode(raw)
        require(result["status"]=="failed" and result["phase"]=="build" and result["error"]=="Refusal: one complete ELF byte extent" and result["execution_attempts"]==[] and result["results"]["builds"]=={} and result["results"]["candidate"]=={} and result["results"]["controls"]=={},"oracle-only failure before host/runtime")
        inv=result["retained_inventory_before_result"]["files"]
        for path,pin in inv.items():
            require(metadata(regular(old/path))==pin,"source packet remains unchanged: "+path)
        old_inputs=decode(regular(old/"input-manifest.json"))
        require(set(old_inputs)==set(self.manifest),"same closed input names for recovery")
        allowed={"new/run-tests.py","new/source-ledger.json","review-lock.json","new/test_dso.c"}
        for path,pin in old_inputs.items():
            if path not in allowed:
                require(pin==self.manifest[path],"unchanged source beyond reviewed oracle/import: "+path)
        require(metadata(regular(old/"inputs/new/run-tests.py"))["sha256"]=="b8a795824c5995252dd62d6be6c5e4821942eb4155fde1043cb88030a470e209","original refused oracle retained")
        generated_old=inventory(old/"generated")["files"]
        generated_new=inventory(self.generated)["files"]
        # Core/loader/DSO sources and the generated receipt/header bytes are identical;
        # source-ledger/runner and the separately reviewed host-only counter definitions
        # may differ. The DSO compiler inputs and their generated header remain exact.
        for path,pin in generated_old.items():
            if path not in ("composition-ledger.json","test_dso.c.added.diff"): require(generated_new.get(path)==pin,"unchanged generated DSO build input: "+path)
        command_names=["compose-source","image-inspect","compiler-version","compiler-target","compile-dso-a-plain","compile-dso-b-plain","compile-dso-call-plain","compile-dso-dep-plain","compile-dso-rootstub-plain","link-dso-a-plain","link-dso-b-plain","link-dso-dep-plain","link-dso-root-plain"]
        require([c["name"] for c in result["commands"]]==command_names,"exact pre-host command history")
        target="/out/build/dsos/plain"; flags=self.flags("plain")
        expected={}
        for label,index in (("a",0),("b",1)):
            expected["compile-dso-"+label+"-plain"]=["gcc",*flags,"-DDP_INDEX="+str(index),"-I/src/new","-I/gen","-c","/src/new/dsomod.c","-o",target+"/dso-"+label+".o"]
        expected["compile-dso-call-plain"]=["gcc","-g","-fPIC","-c","/src/new/dso_call.S","-o",target+"/dso_call.o"]
        for label in ("dep","rootstub"):
            expected["compile-dso-"+label+"-plain"]=["gcc",*flags,"-I/src/new","-I/gen","-c","/src/new/"+label+".c","-o",target+"/"+label+".o"]
        for label in ("a","b"):
            expected["link-dso-"+label+"-plain"]=["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_"+label+".so",target+"/dso-"+label+".o",target+"/dso_call.o","-o",target+"/libdp_"+label+".so"]
        expected["link-dso-dep-plain"]=["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_dep.so",target+"/dep.o","-o",target+"/libdp_dep.so"]
        expected["link-dso-root-plain"]=["gcc",*flags,"-shared","-Wl,-z,now","-Wl,-soname,libdp_root.so",target+"/rootstub.o","-L"+target,"-ldp_dep","-Wl,-rpath,$ORIGIN","-o",target+"/libdp_root.so"]
        retained=[]
        for c in result["commands"]:
            receipt=regular(old/"commands"/c["directory"]/"receipt.json")
            require(digest(receipt)==c["receipt_sha256"] and decode(receipt)=={k:v for k,v in c.items() if k!="receipt_sha256"},"bound original command receipt")
            require(c["returncode"]==0 and c["exception"] is None and c["cleanup"]=={} and c["stderr"]==metadata(b""),"successful original preparation command")
            if c["name"] in expected:
                argv=c["argv"]; position=argv.index("gcc")
                require(argv[position:]==expected[c["name"]],"exact unchanged original compile/link flags")
                require(IMAGE_ID in argv and "--pull=never" in argv and "--network=none" in argv and "--read-only" in argv and c["stdout"]==metadata(b""),"original isolated clean compiler command")
                retained.append({"name":c["name"],"receipt_sha256":c["receipt_sha256"],"stdout":c["stdout"],"stderr":c["stderr"]})
        artifacts=("dso-a.o","dso-b.o","dso_call.o","dep.o","rootstub.o","libdp_a.so","libdp_b.so","libdp_dep.so","libdp_root.so")
        prefix="work/build/dsos/plain/"
        require({x for x in inv if x.startswith("work/build/")}=={prefix+x for x in artifacts},"exact nine pre-oracle artifact inventory")
        folder=self.work/"build"/"dsos"/"plain"; folder.mkdir(parents=True)
        copied={}
        for file in artifacts:
            data=regular(old/prefix/file)
            with (folder/file).open("xb") as out: out.write(data)
            copied[file]=metadata(data)
        receipt={"schema":1,"scope":"one reviewed executable-section oracle correction; unchanged plain DSO compilation reused", "source_packet":name,"source_result":result_name,"source_result_sha256":digest(raw),"source_inventory_sha256":digest(encoded(result["retained_inventory_before_result"])),"source_runner_sha256":old_inputs["new/run-tests.py"]["sha256"],"new_runner_sha256":self.manifest["new/run-tests.py"]["sha256"],"unchanged_compiled_inputs":{k:v for k,v in self.manifest.items() if k not in allowed},"changed_host_only_input":{"name":"new/test_dso.c","before":old_inputs["new/test_dso.c"],"after":self.manifest["new/test_dso.c"]},"commands":retained,"artifacts":copied,"compiler_replayed":False,"host_or_runtime_predecessor":False}
        second_name="run-73d6159394aa459aa2b4d6e15e243000"
        second=HERE/"private"/second_name
        second_result_name="result-28a621cc0a6340a487ccf3ac97169122.json"
        second_raw=regular(second/second_result_name)
        require(digest(second_raw)=="e0d5c8142ba59333921c2ce9f8ec6f032fa6699b5dc810f204775af227623281","reviewed second failed result pin")
        second_result=decode(second_raw)
        require(second_result["status"]=="failed" and second_result["phase"]=="build" and second_result["error"]=="Refusal: clean successful command: link-host-candidate-plain" and second_result["execution_attempts"]==[] and set(second_result["results"]["builds"])=={"dsos-plain"},"second packet completed only DSO build")
        for path,pin in second_result["retained_inventory_before_result"]["files"].items():
            require(metadata(regular(second/path))==pin,"second failure packet remains unchanged: "+path)
        second_inputs=decode(regular(second/"input-manifest.json"))
        require(set(second_inputs)==set(self.manifest),"second closed input names")
        for path,pin in second_inputs.items():
            if path not in allowed: require(pin==self.manifest[path],"second unchanged compiled DSO input: "+path)
        require(second_inputs["new/run-tests.py"]["sha256"]=="d6736127f0e14f7db94c2440a59dce5ea8fbfe9ff3b3e08d33a0358d103c2ca1","corrected oracle source pin")
        prior_receipt=regular(second/"dso-reuse.json")
        require(metadata(prior_receipt)==second_result["results"]["dso_reuse"]["receipt"] and decode(prior_receipt)["source_result_sha256"]==digest(raw),"second adoption binds first compilation")
        completed=second_result["results"]["builds"]["dsos-plain"]
        extra_names={"elf-manifest.json","expected-symbols.h"}|{n+suffix for n in SYMBOLS for suffix in (".elf.txt",".disassembly.txt")}
        require(set(completed["artifacts"])=={"build/dsos/plain/"+n for n in set(artifacts)|extra_names} and completed["actual_blr_marker"] is True and completed["typed_symbols_and_phdrs"] is True,"exact completed DSO artifact closure")
        metadata_commands=[]
        expected_metadata={}
        for file in SYMBOLS:
            label=file.replace("_","-").replace(".so","")
            expected_metadata["plain-"+label+"-elf"]=["readelf","-W","-h","-l","-s","-d",target+"/"+file]
            expected_metadata["plain-"+label+"-disassembly"]=["objdump","-d",target+"/"+file]
        expected_history=["compose-source",*expected_metadata,"compile-host-candidate-plain-core","compile-host-candidate-plain-loader","compile-host-candidate-plain-test-dso","compile-host-candidate-plain-fixture","link-host-candidate-plain"]
        require([c["name"] for c in second_result["commands"]]==expected_history,"exact second command closure")
        for c in second_result["commands"]:
            if c["name"] not in expected_metadata: continue
            rec=regular(second/"commands"/c["directory"]/"receipt.json")
            require(digest(rec)==c["receipt_sha256"] and decode(rec)=={k:v for k,v in c.items() if k!="receipt_sha256"},"bound successful metadata receipt")
            require(c["returncode"]==0 and c["exception"] is None and c["cleanup"]=={} and c["stderr"]==metadata(b""),"successful first metadata retention")
            command=expected_metadata[c["name"]]; argv=c["argv"]
            require(argv[argv.index(command[0]):]==command and IMAGE_ID in argv and "--network=none" in argv and "--pull=never" in argv,"exact metadata command")
            metadata_commands.append({"name":c["name"],"receipt_sha256":c["receipt_sha256"],"stdout":c["stdout"],"stderr":c["stderr"]})
        extras={}
        for path,pin in completed["artifacts"].items():
            data=regular(second/"work"/path); require(metadata(data)==pin,"completed DSO metadata bytes")
            filename=Path(path).name
            if filename in artifacts:
                require(metadata(regular(folder/filename))==pin,"same first-packet DSO artifact")
            else:
                with (folder/filename).open("xb") as out: out.write(data)
                extras[filename]=pin
        receipt["metadata_adoption"]={"source_packet":second_name,"source_result":second_result_name,"source_result_sha256":digest(second_raw),"source_inventory_sha256":digest(encoded(second_result["retained_inventory_before_result"])),"source_runner_sha256":second_inputs["new/run-tests.py"]["sha256"],"prior_reuse_receipt":metadata(prior_receipt),"commands":metadata_commands,"artifacts":extras,"metadata_commands_replayed":False,"host_artifacts_imported":False}
        save(self.path/"dso-reuse.json",receipt)
        self.results["dso_reuse"]={"receipt":metadata(encoded(receipt)),"source_result_sha256":digest(raw),"metadata_source_result_sha256":digest(second_raw),"adopted_commands":len(retained),"adopted_artifacts":len(copied),"adopted_metadata_commands":len(metadata_commands),"adopted_metadata_artifacts":len(extras),"compiler_replayed":False,"metadata_commands_replayed":False}
        self.results["image"]=result["results"]["image"]
        self.results["toolchain"]=result["results"]["toolchain"]
        self.results["builds"]["dsos-plain"]=completed

    def build_host(self,variant,mode,macro=None):
        key=variant+"-"+mode
        require(key not in self.results["builds"],"successful host build replay refused")
        require("dsos-"+mode in self.results["builds"],"pinned DSO preparation precedes host")
        folder=self.work/"build"/variant/mode; folder.mkdir(parents=True)
        target="/out/"+folder.relative_to(self.work).as_posix(); flags=self.flags(mode,True,macro)
        manifest="/out/build/dsos/"+mode
        for name in ("core","loader","test_dso"):
            self.clean("compile-host-"+variant+"-"+mode+"-"+name.replace("_","-"),["gcc",*flags,"-I/src/new","-I/gen","-I"+manifest,"-c","/src/new/"+name+".c","-o",target+"/"+name+".o"],empty_stdout=True)
        self.clean("compile-host-"+variant+"-"+mode+"-fixture",["gcc","-g","-fno-pie","-c","/src/frozen/receipt/fixture.S","-o",target+"/fixture.o"],empty_stdout=True)
        self.clean("link-host-"+variant+"-"+mode,["gcc",*flags,"-no-pie","-rdynamic",target+"/core.o",target+"/loader.o",target+"/test_dso.o",target+"/fixture.o","-ldl","-o",target+"/test-dso"],empty_stdout=True)
        elf=Elf(regular(folder/"test-dso"),2); original_symbol(elf)
        for name in ("cc_claim_hook","dp_fixture_input_get"): symbol(elf,name,2)
        require(not any(s["name"]=="cc_claim_authored" and s["section"] for s in elf.symbols),"authored RA entry absent")
        core=Elf(regular(folder/"core.o"),1); original_symbol(core)
        require(not any(s["name"].startswith(("rc_fixture_","cc_fixture_")) for s in core.symbols),"old fixture seams absent")
        save(folder/"elf-symbols.json",elf.summary())
        for kind,argv in (("elf",["readelf","-W","-h","-l","-s",target+"/test-dso"]),("core-disassembly",["objdump","-dr",target+"/core.o"]),("linked-disassembly",["objdump","-d",target+"/test-dso"])):
            raw=self.clean(variant+"-"+mode+"-"+kind,argv); (folder/(kind+".txt")).write_bytes(raw)
        save(folder/"configuration.json",{"flags":flags,"control_macro":macro,"RC_FIXTURE":False,"CC_FIXTURE":False,"DP_FIXTURE":True,"generated":inventory(self.generated)["files"],"dso_manifest":metadata(regular(self.work/"build"/"dsos"/mode/"elf-manifest.json"))})
        artifacts={p.relative_to(self.work).as_posix():metadata(regular(p)) for p in folder.iterdir() if p.is_file()}
        self.results["builds"][key]={"kind":"host","mode":mode,"variant":variant,"macro":macro,"binary":target+"/test-dso","artifacts":artifacts,"ordinary_builtin_ra":True,"old_fixture_seams":False}

    def build(self):
        self.phase="build"
        modes=select(self.args.modes,("plain","asan-ubsan"),"modes")
        variants=select(self.args.variants,("candidate",*CONTROL_MACROS),"variants")
        if self.args.reuse_plain_dso_packet:
            require("plain" in modes,"plain adoption requires plain selection")
            self.adopt_plain_dsos()
        if "image" not in self.results: self.results["image"]=self.inspect_image()
        if "toolchain" not in self.results:
            v=self.clean("compiler-version",["gcc","--version"]); t=self.clean("compiler-target",["gcc","-dumpmachine"])
            require(b"7.5.0" in v.splitlines()[0] and t.strip()==b"aarch64-linux-gnu","pinned compiler/target")
            self.results["toolchain"]={"version":metadata(v),"target":t.decode().strip()}
        for mode in modes:
            if "dsos-"+mode not in self.results["builds"]: self.build_dsos(mode)
            for variant in variants:
                self.build_host(variant,mode,CONTROL_MACROS.get(variant))
        self.check_inputs()

    def execute(self,variant,mode,selector,failures):
        self.phase="run-"+variant+"-"+mode+"-"+selector
        key=variant+"-"+mode
        require(key in self.results["builds"],"reviewed retained build required")
        name="run-"+key+"-"+selector
        require(name not in {c["name"] for c in self.commands},"selector replay refused")
        folder=self.work/"evidence"/variant/mode/selector; folder.mkdir(parents=True)
        build=self.results["builds"][key]
        row={"variant":variant,"mode":mode,"selector":selector,"returned":False,"accepted":False}
        self.attempts.append(row)
        rc,raw,err=self.docker(name,[build["binary"],selector,"/evidence/report.json","/build"],SAN_ENV if mode=="asan-ubsan" else {},runtime_build=self.work/"build"/"dsos"/mode,evidence=folder)
        row.update(returned=True,returncode=rc)
        checks=self.contract["selectors"][selector]
        expected={"schema":1,"selector":selector,"checks":len(checks),"check_names":checks,"failures":len(failures),"failed_checks":failures,"target_invocations":0}
        value=decode(raw)
        require(value==expected and type(value["checks"]) is int and type(value["failures"]) is int,"exact full named outcome")
        require(raw==(json.dumps(expected,separators=(",",":"))+"\n").encode(),"canonical report bytes")
        require(rc==(1 if failures else 0) and err==b"","exact exit/no sanitizer diagnostics")
        inv=inventory(folder)
        require(set(inv["files"])=={"report.json"} and not inv["directories"],"one raw evidence file")
        report=regular(folder/"report.json"); require(0<len(report)<=MAX_REPORT,"bounded evidence")
        facts=decode(report)
        require(type(facts) is dict and set(facts)=={*expected,"main_lo","main_hi","lifecycle_status","dropped_claims","target_hits","replacement_hits","loader","module","calls","receipt"} and facts["schema"]==1,"closed evidence top-level schema")
        require(all(facts[k]==v for k,v in expected.items()),"raw/report check agreement")
        require(type(facts["loader"]) is dict and type(facts["module"]) is dict and type(facts["calls"]) is list and type(facts["receipt"]) is dict,"raw fact containers")
        require(len(facts["calls"])<=16 and facts["dropped_claims"]==0,"bounded lossless raw calls")
        for key in ("target_hits","replacement_hits"):
            require(type(facts[key]) is list and len(facts[key])==4 and all(type(x) is int and x==0 for x in facts[key]),"four zero actual invocation counters")
        require(facts["target_invocations"]==sum(facts["target_hits"])+sum(facts["replacement_hits"]),"computed invocation total")
        require(all(type(facts[k]) is int and 0<=facts[k]<(1<<64) for k in ("main_lo","main_hi","lifecycle_status")),"raw scalar domains")
        row["accepted"]=True
        # Selected module/receipt details stay private; allowlist only semantic names and byte pins.
        out={"report":expected,"returncode":rc,"evidence":metadata(report),"stdout":metadata(raw),"stderr":metadata(err),"ordinary_builtin_ra_entry":True,"patched_target_or_replacement_calls":0}
        bucket="candidate" if variant=="candidate" else "controls"
        self.results[bucket].setdefault(mode,{})[selector if bucket=="candidate" else variant]=out
        self.check_inputs()
        for b in self.results["builds"].values():
            for path,pin in b["artifacts"].items(): require(metadata(regular(self.work/path))==pin,"post-run immutable artifact")

    def run(self):
        modes=select(self.args.modes,("plain","asan-ubsan"),"modes")
        variants=select(self.args.variants,("candidate",*CONTROL_MACROS),"variants")
        selectors=select(self.args.selectors,self.contract["selectors"],"selectors")
        for mode in modes:
            for variant in variants:
                if variant=="candidate":
                    for selector in selectors: self.execute(variant,mode,selector,[])
                else:
                    c=self.contract["controls"][variant]
                    self.execute(variant,mode,c["selector"],c["expected_failures"])
        self.phase="complete-selected-runtime"

    def perform(self):
        if self.args.packet: self.resume()
        else:
            require(self.args.phase in ("prepare","all"),"new packet must prepare first")
            self.prepare()
        if self.args.phase in ("build","all"): self.build()
        if self.args.phase in ("run","all"): self.run()
        return {"schema":1,"status":"passed","phase":self.phase,"scope":"new fixed authored DSO provenance and custody","runtime_gate":"CLOSED","inputs":self.manifest,"results":self.results,"commands":self.commands,"execution_attempts":self.attempts,"old_matrix_or_composer_replay":False,"native_firmware_or_device_access":False}

def sanitized(result,private_hash):
    r=result.get("results",{})
    return {"schema":1,"status":result["status"],"runtime_gate":"CLOSED","scope":"new fixed authored DSO provenance and custody","private_result_sha256":private_hash,"inputs":result.get("inputs",{}),"candidate":r.get("candidate",{}),"controls":r.get("controls",{}),"builds":{k:{"kind":v["kind"],"mode":v["mode"],"artifacts":v["artifacts"]} for k,v in r.get("builds",{}).items()},"commands":[{"name":c["name"],"returncode":c["returncode"],"exception_present":c["exception"] is not None,"stdout":c["stdout"],"stderr":c["stderr"],"receipt_sha256":c["receipt_sha256"]} for c in result.get("commands",[])],"execution_attempts":result.get("execution_attempts",[]),"output_streams":2*len(result.get("commands",[])),"retained_files_including_private_result":result.get("retained_files_including_result"),"image":r.get("image"),"toolchain":r.get("toolchain"),"dso_reuse":r.get("dso_reuse"),"actual_builtin_ra_executed":bool(r.get("candidate") or r.get("controls")),"RC_FIXTURE":False,"CC_FIXTURE":False,"DP_FIXTURE":True,"old_matrix_or_composer_replay":False,"native_firmware_or_device_access":False,"generated_code_invoked":False,"live_gate":"CLOSED"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--docker",default=shutil.which("docker") or "/opt/homebrew/bin/docker")
    p.add_argument("--phase",choices=("prepare","build","run","all"),default="prepare")
    p.add_argument("--reuse-plain-dso-packet",help="only the separately reviewed first oracle-failure packet")
    p.add_argument("--packet"); p.add_argument("--modes"); p.add_argument("--variants"); p.add_argument("--selectors")
    args=p.parse_args(); run=Run(args); result=None; status=1
    try: result=run.perform(); status=0
    except BaseException as exc:
        result={"schema":1,"status":"failed","phase":run.phase,"error":type(exc).__name__+": "+str(exc),"traceback":traceback.format_exc(),"inputs":run.manifest,"results":run.results,"commands":run.commands,"execution_attempts":run.attempts,"runtime_gate":"CLOSED"}
    finally:
        if run.path.exists():
            try:
                run.snapshot(); result["retained_inventory_before_result"]=inventory(run.path)
                result["retained_files_including_result"]=len(result["retained_inventory_before_result"]["files"])+1
            except BaseException as exc: result["inventory_error"]=repr(exc); result["status"]="failed"; status=1
            name="result-"+run.run_id+".json"; save(run.path/name,result)
            projected=sanitized(result,digest(encoded(result))); save(run.path/("sanitized-"+run.run_id+".json"),projected)
            print(json.dumps({"status":result["status"],"packet":run.path.name,"result":name,"private_result_sha256":digest(encoded(result)),"sanitized_result_sha256":digest(encoded(projected))},sort_keys=True))
        else: print("refused before packet creation",file=sys.stderr)
    return status

if __name__=="__main__": sys.exit(main())
