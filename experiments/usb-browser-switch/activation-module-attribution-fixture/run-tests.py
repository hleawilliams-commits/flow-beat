#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New immutable module/generated attribution only; completed DSO artifacts are reused."""
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
PLAN = PARENT / "activation-module-attribution-plan/composition-plan.json"
PLAN_SHA = "1bd883758d6bfce467c6aae9e57fa0c0d2196e06edf020049021d4b26f2808d4"
PLAN_README_SHA = "972ce22f04e0827a3ca56a6a87c2bba0667956933e676a775667314ed7df5d9f"
SOURCE_NAMES = ("run-tests.py", "compose-source.py", "core.c", "core.h", "attribution.h",
                "attribution.inc", "core-transformations.json", "loader.c", "loader.h",
                "test_module.c", "test-contract.json")
PRIOR = PARENT / "activation-dso-provenance-fixture"
PRIOR_PACKET = PRIOR / "private/run-392f285161d24a36b88ce82e754c0e6b"
PRIOR_RESULT = "result-b42106cbb06c4015a4b2e12dbb78a0f9.json"
PRIOR_RESULT_SHA = "ba02e5d67e410160c1db5b2092395064e0a0459e9543d8709f55dc1ff000d05c"
# The first standalone compilation succeeded; the initial call oracle refused
# the emitted stack-canary failure dependency. Only those two artifacts may be
# adopted, and the corrected guard classification is a new review result.
STANDALONE_PACKET = "run-07909828d41e4eb5ad9436a735d952c6"
STANDALONE_RESULT = "result-a183a458aabb493386b3ade0ccd4d75a.json"
STANDALONE_RESULT_SHA = "4874e3c32ca25e01acc43e7f54e13c50aec61d6931bdfe3971307f8a19ba181c"
STANDALONE_RUNNER_SHA = "aff75d39e5cbcf0427537c6c84dae308ba37cbbf688b9908d089e9e5fbc5325f"
GUARD_BODY = {"bytes":3275,"sha256":"d07ab1db0d7bdc9ba65655de3371c450fdde26c5f4ab5fad79bc21dfae9de416"}
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-pthread"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {
    "stop-at-hole":"MA_CONTROL_STOP_AT_HOLE",
    "consume-raw-query-for-publication":"MA_CONTROL_RAW_QUERY",
    "omit-descriptor-case-publication":"MA_CONTROL_OMIT_DESCRIPTOR_PUBLICATION",
    "clear-fault-visibility-after-init-error":"MA_CONTROL_CLEAR_INIT",
    "exec-only-module-envelope":"MA_CONTROL_EXEC_ONLY",
    "generated-before-module":"MA_CONTROL_GENERATED_FIRST",
    "omit-process-identity-match":"MA_CONTROL_OMIT_PROCESS",
    "omit-staging-id-copy":"MA_CONTROL_OMIT_ID_COPY"}
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
MODEL_INSERTION = {"ma_model_stage","ma_model_commit","ma_model_record","ma_model_hold_locks","ma_model_release_locks"}
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
    require(type(value["selectors"]) is dict and len(value["selectors"])==18 and type(value["controls"]) is dict and set(value["controls"])==set(CONTROL_MACROS),"eighteen selectors/eight controls")
    for name,row in value["selectors"].items():
        require(SAFE_NAME.fullmatch(name) and type(row) is dict and set(row)=={"configuration","checks"} and row["configuration"] in ("actual","model"),"separate actual/model selector")
        checks=row["checks"]
        require(type(checks) is list and checks and len(checks)==len(set(checks)) and all(SAFE_NAME.fullmatch(x) for x in checks),"unique checks")
    for name,c in value["controls"].items():
        require(type(c) is dict and set(c)=={"macro","selector","configuration","expected_failures","safety"},"control shape")
        require(c["macro"]==CONTROL_MACROS[name],"exact control macro")
        require(c["selector"] in value["selectors"] and c["configuration"]==value["selectors"][c["selector"]]["configuration"],"control configuration matches selector")
        require(c["selector"] in value["selectors"] and c["expected_failures"] and len(c["expected_failures"])==len(set(c["expected_failures"])) and all(x in value["selectors"][c["selector"]]["checks"] for x in c["expected_failures"]),"exact safe failures")
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
            self.put("new/"+name,raw,"new immutable module attribution fixture")
        planraw=regular(PLAN); require(digest(planraw)==PLAN_SHA,"frozen plan pin"); plan=decode(planraw)
        self.put("plan/composition-plan.json",planraw,"frozen reviewed plan")
        planreadme=regular(PLAN.parent/"README.md"); require(digest(planreadme)==PLAN_README_SHA,"frozen plan README pin")
        self.put("plan/README.md",planreadme,"frozen reviewed plan")
        for row in plan["sources"]:
            raw=regular(REPO/row["path"])
            require(metadata(raw)=={k:row[k] for k in ("bytes","sha256")},"reused plan source pin: "+row["name"])
            self.put("frozen/"+row["name"],raw,"reused retained source only")
        self.adopt_dsos()
        if self.args.reuse_standalone: self.adopt_standalone()
        save(self.path/"input-manifest.json",self.manifest)
        self.contract=contract_from(regular(self.inputs/"new/test-contract.json"))
        expected_controls={x["id"]:x["expected_failures"] for x in plan["safe_controls"]}
        require(set(expected_controls)==set(self.contract["controls"]) and all(self.contract["controls"][n]["expected_failures"]==v for n,v in expected_controls.items()),"exact reviewed-plan control failure sets")
        require(sum(x["configuration"]=="actual" for x in self.contract["selectors"].values())==7 and sum(x["configuration"]=="model" for x in self.contract["selectors"].values())==11,"seven actual and eleven model selectors")
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
        container = "az-module-attribution-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
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


    def flags(self, mode, configuration=None, macro=None):
        flags=COMMON+["-fno-pie"]
        if configuration is not None: flags += ["-DMA_FIXTURE=1"]
        if configuration=="model": flags += ["-DMA_MODEL=1"]
        if mode=="asan-ubsan": flags += SAN_FLAGS
        if macro: flags += ["-D"+macro+"=1"]
        return flags

    def adopt_dsos(self):
        """Exact completed fixture artifacts and selected provenance, no old commands."""
        raw=regular(PRIOR_PACKET/PRIOR_RESULT)
        require(digest(raw)==PRIOR_RESULT_SHA,"completed predecessor result pin")
        prior=decode(raw)
        require(prior["status"]=="passed" and prior["runtime_gate"]=="CLOSED" and prior["phase"]=="complete-selected-runtime","completed predecessor scope")
        inv=prior["retained_inventory_before_result"]["files"]
        def retained(packet,path,pins):
            data=regular(packet/path)
            require(metadata(data)==pins[path],"retained predecessor byte pin: "+path)
            return data
        self.put("frozen/provenance/dso-result.json",raw,"completed predecessor result; no replay")
        imraw=retained(PRIOR_PACKET,"input-manifest.json",inv)
        im=decode(imraw)
        require(im==prior["inputs"],"completed predecessor input identity")
        self.put("frozen/provenance/dso-input-manifest.json",imraw,"predecessor compiled input pins")
        support={"frozen/receipt/pinned-community.inc":"generated/pinned-community.inc",
                 "frozen/receipt/fixture.S":"inputs/frozen/receipt/fixture.S",
                 "frozen/dso/dp_fixture.h":"inputs/new/dp_fixture.h",
                 "frozen/dso/dsomod.c":"inputs/new/dsomod.c",
                 "frozen/dso/dso_call.S":"inputs/new/dso_call.S",
                 "frozen/dso/dep.c":"inputs/new/dep.c",
                 "frozen/dso/rootstub.c":"inputs/new/rootstub.c"}
        for dest,source in support.items():
            self.put(dest,retained(PRIOR_PACKET,source,inv),"unchanged retained DSO or host custody source")
        for name in ("dsomod.c","dso_call.S","dep.c","rootstub.c","dp_fixture.h"):
            require(metadata(regular(PRIOR/name))=={k:im["new/"+name][k] for k in ("bytes","sha256")},"current frozen DSO source pin")
        require(metadata(regular(self.inputs/"frozen/sdk/azmod-mod.h"))=={k:im["frozen/sdk/azmod-mod.h"][k] for k in ("bytes","sha256")},"same exact DSO SDK header")
        prior_reuse=retained(PRIOR_PACKET,"dso-reuse.json",inv)
        reuse=decode(prior_reuse)
        require(metadata(prior_reuse)==prior["results"]["dso_reuse"]["receipt"],"bound predecessor adoption chain")
        self.put("frozen/provenance/prior-dso-reuse.json",prior_reuse,"prior clean compile and metadata adoption")
        command_pins=[]
        def bind_commands(packet,result,names,label):
            pins=result["retained_inventory_before_result"]["files"]
            selected=[c for c in result["commands"] if c["name"] in names]
            require(len(selected)==len(names) and {c["name"] for c in selected}==set(names),"exact selected predecessor commands")
            for c in selected:
                prefix="commands/"+c["directory"]+"/"
                receipt=retained(packet,prefix+"receipt.json",pins)
                require(digest(receipt)==c["receipt_sha256"] and decode(receipt)=={k:v for k,v in c.items() if k!="receipt_sha256"},"predecessor command receipt identity")
                require(c["returncode"]==0 and c["exception"] is None and c["cleanup"]=={} and c["stderr"]==metadata(b""),"successful selected predecessor command")
                require(IMAGE_ID in c["argv"] and "--pull=never" in c["argv"] and "--network=none" in c["argv"] and "--read-only" in c["argv"],"isolated pinned predecessor command")
                for leaf in ("receipt.json","stdout.bin","stderr.bin"):
                    data=retained(packet,prefix+leaf,pins)
                    if leaf!="receipt.json": require(metadata(data)==c[leaf[:-4]],"bound predecessor stream")
                    self.put("frozen/provenance/commands/"+label+"/"+c["name"]+"/"+leaf,data,"selected successful predecessor receipt; not replayed")
                command_pins.append({"name":c["name"],"receipt_sha256":c["receipt_sha256"],"source":label})
        # Plain artifacts retain their first successful compiler and metadata receipts.
        for key,label in ((None,"plain-compiler"),("metadata_adoption","plain-metadata")):
            item=reuse if key is None else reuse[key]
            packet=PRIOR/"private"/item["source_packet"]
            oldraw=regular(packet/item["source_result"])
            require(digest(oldraw)==item["source_result_sha256"],"retained prior failure result identity")
            self.put("frozen/provenance/"+label+"-source-result.json",oldraw,"preserved earlier configuration failure")
            bind_commands(packet,decode(oldraw),[x["name"] for x in item["commands"]],label)
        asan_names=[c["name"] for c in prior["commands"] if (c["name"].startswith(("compile-dso-","link-dso-")) and c["name"].endswith("-asan-ubsan")) or c["name"].startswith("asan-ubsan-libdp-")]
        require(len(asan_names)==17,"nine ASan DSO compiler/link and eight metadata receipts")
        bind_commands(PRIOR_PACKET,prior,asan_names,"asan-ubsan")
        copied={}
        filenames={"dso-a.o","dso-b.o","dso_call.o","dep.o","rootstub.o","elf-manifest.json","expected-symbols.h"}|set(SYMBOLS)|{n+s for n in SYMBOLS for s in (".elf.txt",".disassembly.txt")}
        for mode in ("plain","asan-ubsan"):
            row=prior["results"]["builds"]["dsos-"+mode]
            require(row["kind"]=="dso" and row["mode"]==mode and row["actual_blr_marker"] is True and row["typed_symbols_and_phdrs"] is True,"completed typed DSO artifacts")
            prefix="build/dsos/"+mode+"/"
            require(set(row["artifacts"])=={prefix+n for n in filenames},"exact nineteen-file DSO artifact set")
            for path,pin in row["artifacts"].items():
                data=retained(PRIOR_PACKET,"work/"+path,inv)
                require(metadata(data)==pin,"completed DSO artifact pin")
                target=self.work/path; target.parent.mkdir(parents=True,exist_ok=True)
                with target.open("xb") as f:f.write(data)
                copied[path]=pin
            self.results["builds"]["dsos-"+mode]={**row,"adopted":True}
        receipt={"schema":1,"source_result_sha256":digest(raw),"source_public_result":metadata(regular(PRIOR/"result.json")),"source_packet":PRIOR_PACKET.name,"source_result":PRIOR_RESULT,"source_inputs_sha256":digest(imraw),"prior_adoption":metadata(prior_reuse),"artifacts":copied,"selected_command_receipts":command_pins,"compiler_or_metadata_commands_replayed":False,"runtime_replayed":False}
        require(decode(regular(PRIOR/"result.json"))["private_result_sha256"]==digest(raw),"published predecessor result link")
        save(self.path/"dso-reuse.json",receipt)
        self.results["dso_reuse"]={"receipt":metadata(encoded(receipt)),"source_result_sha256":digest(raw),"artifact_count":len(copied),"selected_command_receipts":len(command_pins),"compiler_or_metadata_commands_replayed":False,"runtime_replayed":False}

    def adopt_standalone(self):
        """Narrow first-failure adoption; no prior compiler or disassembler runs."""
        old=HERE/"private"/STANDALONE_PACKET
        raw=regular(old/STANDALONE_RESULT)
        require(digest(raw)==STANDALONE_RESULT_SHA,"pinned initial standalone oracle failure")
        result=decode(raw); pins=result["retained_inventory_before_result"]["files"]
        require(result["status"]=="failed" and result["phase"]=="build" and result["runtime_gate"]=="CLOSED" and result["error"]=="Refusal: reviewed reader direct call dependency: __stack_chk_fail","exact preserved guard-oracle failure")
        require(result["execution_attempts"]==[] and result["results"]["candidate"]=={} and result["results"]["controls"]=={} and set(result["results"]["builds"])=={"dsos-plain","dsos-asan-ubsan"},"no previous host or runtime work")
        require([c["name"] for c in result["commands"]]==["compose-source","image-inspect","compiler-version","compiler-target","compile-standalone-plain","standalone-plain-disassembly"],"exact first-attempt command history")
        def retained(name):
            data=regular(old/name)
            require(metadata(data)==pins[name],"pinned standalone source artifact: "+name)
            return data
        self.put("frozen/standalone-reuse/failed-result.json",raw,"initial failed oracle result remains failed")
        imraw=retained("input-manifest.json"); im=decode(imraw)
        require(im==result["inputs"] and set(im)==set(self.manifest)-{"frozen/standalone-reuse/failed-result.json"},"exact prior input inventory")
        changed={"new/run-tests.py","new/core-transformations.json","new/source-ledger.json","review-lock.json"}
        for name,pin in im.items():
            data=retained("inputs/"+name)
            require(metadata(data)=={k:pin[k] for k in ("bytes","sha256")},"prior input byte identity")
            if name in changed:
                self.put("frozen/standalone-reuse/previous-inputs/"+name,data,"preserved previous uncompiled review source")
            else:
                require(regular(self.inputs/name)==data,"unchanged compiled and supporting input: "+name)
        require(im["new/run-tests.py"]["sha256"]==STANDALONE_RUNNER_SHA,"exact previous runner source")
        self.put("frozen/standalone-reuse/input-manifest.json",imraw,"previous complete immutable input pins")
        prepraw=retained("source-preparation.json"); prep=decode(prepraw)
        require(inventory(old/"generated")==prep["generated"],"previous generated source closure")
        self.put("frozen/standalone-reuse/source-preparation.json",prepraw,"previous generated dependency pins")
        selected=result["commands"][-2:]; command_pins=[]
        expected={"compile-standalone-plain":["gcc",*self.flags("plain"),"-I/src/new","-I/gen","-c","/src/new/core.c","-o","/out/build/standalone/plain/core.o"],"standalone-plain-disassembly":["objdump","-dr","/out/build/standalone/plain/core.o"]}
        streams={}
        for c in selected:
            name=c["name"]; argv=c["argv"]; tail=expected[name]
            require(argv[-len(tail):]==tail and IMAGE_ID in argv and all(x in argv for x in ("--pull=never","--network=none","--read-only","--platform=linux/arm64")),"exact isolated standalone command and unchanged flags")
            require(c["returncode"]==0 and c["exception"] is None and c["cleanup"]=={} and c["stderr"]==metadata(b""),"successful original standalone command")
            prefix="commands/"+c["directory"]+"/"
            receipt=retained(prefix+"receipt.json")
            require(digest(receipt)==c["receipt_sha256"] and decode(receipt)=={k:v for k,v in c.items() if k!="receipt_sha256"},"original standalone receipt identity")
            for leaf in ("command.json","receipt.json","stdout.bin","stderr.bin"):
                data=retained(prefix+leaf)
                if leaf in ("stdout.bin","stderr.bin"): require(metadata(data)==c[leaf[:-4]],"original standalone stream identity")
                self.put("frozen/standalone-reuse/commands/"+name+"/"+leaf,data,"successful command retained without replay")
                if leaf=="stdout.bin": streams[name]=data
            command_pins.append({"name":name,"receipt_sha256":c["receipt_sha256"]})
        require(streams["compile-standalone-plain"]==b"","original clean compile stdout")
        artifacts={}
        for leaf in ("core.o","core-disassembly.txt"):
            relative="build/standalone/plain/"+leaf; data=retained("work/"+relative)
            if leaf=="core-disassembly.txt": require(data==streams["standalone-plain-disassembly"],"original disassembly stream/artifact identity")
            target=self.work/relative; target.parent.mkdir(parents=True,exist_ok=True)
            with target.open("xb") as f: f.write(data)
            artifacts[relative]=metadata(data)
        expected_work={"work/"+name for b in result["results"]["builds"].values() for name in b["artifacts"]}|{"work/"+name for name in artifacts}
        require({name for name in pins if name.startswith("work/")}==expected_work,"only prior DSOs and two standalone artifacts")
        receipt={"schema":1,"source_packet":STANDALONE_PACKET,"source_result":STANDALONE_RESULT,"source_result_sha256":digest(raw),"source_status":"failed","prior_runner_sha256":STANDALONE_RUNNER_SHA,"source_inputs":metadata(imraw),"source_preparation":metadata(prepraw),"artifacts":artifacts,"commands":command_pins,"flags":self.flags("plain"),"changed_uncompiled_inputs":sorted(changed),"compiler_or_disassembler_replayed":False,"new_guard_review_pending":True}
        save(self.path/"standalone-reuse.json",receipt)
        self.results["standalone_reuse"]={"receipt":metadata(encoded(receipt)),"source_result_sha256":digest(raw),"source_status":"failed","artifact_count":2,"selected_command_receipts":2,"compiler_or_disassembler_replayed":False}

    def reused_standalone(self):
        raw=regular(self.path/"standalone-reuse.json")
        require(metadata(raw)==self.results["standalone_reuse"]["receipt"],"immutable standalone adoption receipt")
        receipt=decode(raw)
        require(receipt["source_result_sha256"]==STANDALONE_RESULT_SHA and receipt["flags"]==self.flags("plain"),"adopted standalone source and flags")
        oldprep=decode(regular(self.inputs/"frozen/standalone-reuse/source-preparation.json"))
        generated=inventory(self.generated)
        require(generated["directories"]==oldprep["generated"]["directories"] and set(generated["files"])==set(oldprep["generated"]["files"]),"unchanged generated inventory")
        require(all(pin==oldprep["generated"]["files"][name] for name,pin in generated["files"].items() if name!="composition-ledger.json"),"unchanged compiled generated bytes and runtime source diffs")
        folder=self.work/"build/standalone/plain"
        require(inventory(folder)["files"]=={Path(name).name:pin for name,pin in receipt["artifacts"].items()},"only the two immutable adopted artifacts before new review")
        return receipt

    def guard_failure(self,name,body,instructions):
        """One reviewed emitted canary edge; never a general helper allowlist."""
        require(name=="ma_generated_index" and metadata(body.encode())==GUARD_BODY,"exact reviewed canary method body")
        words={0x23c8:"90000001",0x23cc:"f9400022",0x23d0:"f90087a2",0x246c:"90000001",0x2470:"f94087a2",0x2474:"f9400021",0x2478:"ca010041",0x247c:"b5000181",0x2488:"d65f03c0",0x24ac:"a90153b3",0x24b0:"f90017b6",0x24b4:"94000000"}
        by_address={i["address"]:i for i in instructions}
        require(all(address in by_address and by_address[address]["word"]==word for address,word in words.items()),"exact saved/reloaded guard and conditional failure words")
        relocations=[{"address":int(m.group(1),16),"kind":m.group(2),"target":m.group(3)} for m in re.finditer(r"(?m)^[ \t]*([0-9a-f]+):[ \t]+R_AARCH64_([A-Z0-9_]+)[ \t]+(\S+)",body) if m.group(3) in ("__stack_chk_guard","__stack_chk_fail")]
        expected=[{"address":a,"kind":k,"target":t} for a,k,t in ((0x23c8,"ADR_PREL_PG_HI21","__stack_chk_guard"),(0x23cc,"LDST64_ABS_LO12_NC","__stack_chk_guard"),(0x246c,"ADR_PREL_PG_HI21","__stack_chk_guard"),(0x2474,"LDST64_ABS_LO12_NC","__stack_chk_guard"),(0x24b4,"CALL26","__stack_chk_fail"))]
        require(relocations==expected and by_address[0x247c]["mnemonic"]=="cbnz" and by_address[0x247c]["operands"]=="x1, 24ac <ma_generated_index+0xf0>" and instructions[-1]["address"]==0x24b4,"sole reviewed mismatch edge and terminal failure call")
        return {"classification":"guard-mismatch-only failure dependency","method_body":metadata(body.encode()),"guard_instructions":[by_address[a] for a in words],"guard_relocations":relocations,"normal_return":0x2488,"mismatch_branch":0x247c,"failure_block":0x24ac,"failure_call":0x24b4,"excluded_from_bounded_normal_read_scope":True,"stack_protection_disabled":False}

    def reader_codegen(self, raw):
        text=raw.decode("utf-8","strict")
        starts=list(re.finditer(r"(?m)^[0-9a-f]+ <([^>]+)>:\n",text))
        bodies={m.group(1):text[m.end():starts[i+1].start() if i+1<len(starts) else len(text)] for i,m in enumerate(starts)}
        require("ma_owner_index" in bodies and "rc_lookup" in bodies,"method-bound reader disassembly")
        names=[n for n in bodies if n in ("ma_owner_index","rc_lookup") or n.startswith("ma_generated_index")]
        publication=[n for n in bodies if n.startswith(("ma_commit_locked","ma_root_register"))]
        require(publication,"method-bound module publication disassembly")
        observed={}
        for name in names+publication:
            body=bodies[name]
            instructions=[]
            for m in re.finditer(r"(?m)^[ \t]*([0-9a-f]+):[ \t]+([0-9a-f]{8})[ \t]+([a-z0-9.]+)[ \t]*(.*?)$",body):
                instructions.append({"address":int(m.group(1),16),"word":m.group(2),"mnemonic":m.group(3),"operands":m.group(4)})
            require(instructions,"nonempty bound codegen extent: "+name)
            relocations={int(m.group(1),16):m.group(3) for m in re.finditer(r"(?m)^\s*([0-9a-f]+):\s+R_AARCH64_(CALL26|JUMP26)\s+(\S+)",body)}
            guard=self.guard_failure(name,body,instructions) if "__stack_chk_fail" in relocations.values() and name in names else None
            calls=[]
            for ins in instructions:
                if ins["mnemonic"] not in ("bl","blr","br","b"): continue
                target=relocations.get(ins["address"])
                if target is None:
                    match=re.search(r"<([^>]+)>",ins["operands"])
                    target=match.group(1).split("+")[0] if match else None
                if ins["mnemonic"]=="b" and target==name: continue
                if name in names:
                    require(ins["mnemonic"] not in ("blr","br") and target is not None,"bounded reader direct calls only")
                    base=target.split(".")[0]
                    allowed={"memcpy"} if name=="rc_lookup" else {"rc_lookup","ma_generated_index","memcmp"}
                    guard_call=(base=="__stack_chk_fail" and guard is not None and ins["address"]==guard["failure_call"] and ins["mnemonic"]=="bl")
                    require(base in allowed or guard_call,"reviewed reader direct call dependency: "+str(target))
                calls.append({"instruction":ins["address"],"mnemonic":ins["mnemonic"],"target":target})
            acquires=[i for i in instructions if i["mnemonic"]=="ldar"]
            releases=[i for i in instructions if i["mnemonic"]=="stlr"]
            if name in names: require(acquires,"LDAR in exact standalone reader extent: "+name)
            observed[name]={"sha256":digest(body.encode()),"instructions":instructions,"direct_calls":calls,"acquire_instructions":acquires,"release_instructions":releases,"guard_failure_dependency":guard}
        require(any(observed[n]["release_instructions"] for n in publication),"STLR in module publication extent")
        return {"schema":1,"scope":"Bounded standalone GCC/Linux instruction and direct-call evidence; dependency order requires independent source/instruction review", "reader_extents":names,"publication_extents":publication,"methods":observed,"method_bound_ldar_and_stlr":True,"direct_call_allowlist_checked":True,"guard_failure_excluded_from_bounded_normal_read_scope":True,"transitive_libc_locks_or_signal_safety_proved":False,"coherent_snapshot_proved":False,"relaxed_atomic_control_executed":False}

    def build_standalone(self,mode):
        key="standalone-"+mode
        require(key not in self.results["builds"],"successful standalone build replay refused")
        folder=self.work/"build"/"standalone"/mode
        target="/out/"+folder.relative_to(self.work).as_posix(); flags=self.flags(mode)
        adopted=mode=="plain" and "standalone_reuse" in self.results
        if adopted:
            self.reused_standalone()
            raw=regular(folder/"core-disassembly.txt")
        else:
            folder.mkdir(parents=True)
            self.clean("compile-standalone-"+mode,["gcc",*flags,"-I/src/new","-I/gen","-c","/src/new/core.c","-o",target+"/core.o"],empty_stdout=True)
            raw=self.clean("standalone-"+mode+"-disassembly",["objdump","-dr",target+"/core.o"])
            (folder/"core-disassembly.txt").write_bytes(raw)
        elf=Elf(regular(folder/"core.o"),1); original_symbol(elf)
        require(not any(s["section"] and (s["name"].startswith(("rc_fixture_","cc_fixture_")) or s["name"] in MODEL_INSERTION) for s in elf.symbols),"standalone excludes old and model insertion seams")
        codegen=self.reader_codegen(raw) if mode=="plain" else {"scope":"Sanitizer-instrumented standalone object retained; ordinary reader codegen checked in plain mode", "runtime_execution":False}
        save(folder/"reader-codegen.json",codegen)
        save(folder/"elf-symbols.json",elf.summary())
        save(folder/"configuration.json",{"flags":flags,"MA_FIXTURE":False,"MA_MODEL":False,"compile_only":True})
        artifacts={p.relative_to(self.work).as_posix():metadata(regular(p)) for p in folder.iterdir() if p.is_file()}
        self.results["builds"][key]={"kind":"standalone","mode":mode,"configuration":"standalone","compile_only":True,"adopted":adopted,"artifacts":artifacts}

    def build_host(self,variant,mode,configuration,macro=None):
        key=variant+"-"+configuration+"-"+mode
        require(key not in self.results["builds"],"successful host build replay refused")
        require("dsos-"+mode in self.results["builds"],"pinned DSO preparation precedes host")
        folder=self.work/"build"/variant/configuration/mode; folder.mkdir(parents=True)
        target="/out/"+folder.relative_to(self.work).as_posix(); flags=self.flags(mode,configuration,macro)
        manifest="/out/build/dsos/"+mode
        for name in ("core","loader","test_module"):
            self.clean("compile-host-"+key+"-"+name.replace("_","-"),["gcc",*flags,"-I/src/new","-I/gen","-I"+manifest,"-c","/src/new/"+name+".c","-o",target+"/"+name+".o"],empty_stdout=True)
        self.clean("compile-host-"+key+"-fixture",["gcc","-g","-fno-pie","-c","/src/frozen/receipt/fixture.S","-o",target+"/fixture.o"],empty_stdout=True)
        self.clean("link-host-"+key,["gcc",*flags,"-no-pie","-rdynamic",target+"/core.o",target+"/loader.o",target+"/test_module.o",target+"/fixture.o","-ldl","-o",target+"/test-module"],empty_stdout=True)
        elf=Elf(regular(folder/"test-module"),2); original_symbol(elf)
        for name in ("cc_claim_hook","dp_fixture_input_get","ma_owner_index","ma_module_snapshot"): symbol(elf,name,2)
        require(not any(s["name"]=="cc_claim_authored" and s["section"] for s in elf.symbols),"authored RA entry absent")
        core=Elf(regular(folder/"core.o"),1); original_symbol(core)
        require(not any(s["name"].startswith(("rc_fixture_","cc_fixture_")) for s in core.symbols),"old fixture seams absent")
        if configuration=="actual": require(not any(s["section"] and s["name"] in MODEL_INSERTION for s in core.symbols),"actual route excludes model insertion symbols")
        save(folder/"elf-symbols.json",elf.summary())
        for kind,argv in (("elf",["readelf","-W","-h","-l","-s",target+"/test-module"]),("core-disassembly",["objdump","-dr",target+"/core.o"]),("linked-disassembly",["objdump","-d",target+"/test-module"])):
            raw=self.clean(key+"-"+kind,argv); (folder/(kind+".txt")).write_bytes(raw)
        save(folder/"configuration.json",{"flags":flags,"control_macro":macro,"RC_FIXTURE":False,"CC_FIXTURE":False,"DP_FIXTURE":False,"MA_FIXTURE":True,"MA_MODEL":configuration=="model","generated":inventory(self.generated)["files"],"dso_manifest":metadata(regular(self.work/"build"/"dsos"/mode/"elf-manifest.json"))})
        artifacts={p.relative_to(self.work).as_posix():metadata(regular(p)) for p in folder.iterdir() if p.is_file()}
        self.results["builds"][key]={"kind":"host","mode":mode,"variant":variant,"configuration":configuration,"macro":macro,"binary":target+"/test-module","artifacts":artifacts,"ordinary_builtin_ra":True,"old_fixture_seams":False}

    def build(self):
        self.phase="build"
        modes=select(self.args.modes,("plain","asan-ubsan"),"modes")
        variants=select(self.args.variants,("standalone","candidate",*CONTROL_MACROS),"build variants")
        configurations=select(self.args.configurations,("actual","model"),"configurations")
        if "image" not in self.results: self.results["image"]=self.inspect_image()
        if "toolchain" not in self.results:
            v=self.clean("compiler-version",["gcc","--version"]); t=self.clean("compiler-target",["gcc","-dumpmachine"])
            require(b"7.5.0" in v.splitlines()[0] and t.strip()==b"aarch64-linux-gnu","pinned compiler/target")
            self.results["toolchain"]={"version":metadata(v),"target":t.decode().strip()}
        selected=0
        for mode in modes:
            for variant in variants:
                if variant=="standalone":
                    self.build_standalone(mode); selected+=1
                elif variant=="candidate":
                    for configuration in configurations:
                        self.build_host(variant,mode,configuration); selected+=1
                else:
                    control=self.contract["controls"][variant]
                    if control["configuration"] in configurations:
                        self.build_host(variant,mode,control["configuration"],control["macro"]); selected+=1
        require(selected>0,"nonempty selected builds")
        self.check_inputs()

    def attribution_shape(self,value,configuration):
        fields={"violation","peek_count","io_count","unblamed","open_flags","open_mode","path_matches","write_requested","payload_length","write_returned","payload","peeks","io","modules","queries","constructors","modeled_generated","consumer_return"}
        require(type(value) is dict and set(value)==fields,"closed attribution evidence fields")
        def uint(x): return type(x) is int and 0<=x<(1<<64)
        def sint(x): return type(x) is int and -(1<<63)<=x<(1<<63)
        def bit(x): return type(x) is int and x in (0,1)
        require(value["violation"]==0 and bit(value["path_matches"]),"lossless bounded attribution providers")
        require(all(uint(value[n]) for n in ("peek_count","io_count","unblamed","open_mode","write_requested","payload_length")) and sint(value["open_flags"]) and sint(value["write_returned"]),"attribution scalar domains")
        require(type(value["payload"]) is str and re.fullmatch(r"(?:[0-9a-f]{2}){0,33}",value["payload"]) and len(value["payload"])==2*value["payload_length"],"bounded copied consumer payload")
        require(type(value["peeks"]) is list and len(value["peeks"])==value["peek_count"]<=64,"bounded complete frame-read trace")
        for row in value["peeks"]: require(type(row) is dict and set(row)=={"address","success"} and uint(row["address"]) and bit(row["success"]),"frame observation shape")
        require(type(value["io"]) is list and len(value["io"])==value["io_count"]<=16,"bounded complete model I/O trace")
        for row in value["io"]: require(type(row) is dict and set(row)=={"kind","fd","length","result"} and type(row["kind"]) is int and row["kind"] in (1,2,3,4) and sint(row["fd"]) and uint(row["length"]) and sint(row["result"]),"model I/O observation shape")
        require(type(value["modules"]) is list and len(value["modules"])==4,"four fixed module observations")
        for index,row in enumerate(value["modules"]):
            require(type(row) is dict and set(row)=={"slot","available","committed","index","process_id","lo","hi","id","id_bytes"},"module observation shape")
            require(row["slot"]==index and bit(row["available"]) and bit(row["committed"]) and all(uint(row[k]) for k in ("index","process_id","lo","hi")),"module observation domains")
            require(type(row["id_bytes"]) is str and re.fullmatch(r"[0-9a-f]{66}",row["id_bytes"]) and type(row["id"]) is str and len(row["id"].encode())<=32,"bounded permanent ID bytes")
            require(bytes.fromhex(row["id_bytes"]).split(b"\0",1)[0]==row["id"].encode(),"copied ID byte/string agreement")
        require(type(value["queries"]) is list and len(value["queries"])<=64,"bounded lookup observations")
        for row in value["queries"]: require(type(row) is dict and set(row)=={"label","address","owner"} and type(row["label"]) is str and 0<len(row["label"])<=128 and uint(row["address"]) and type(row["owner"]) is int and -1<=row["owner"]<4,"numeric lookup observation shape")
        require(type(value["constructors"]) is list and len(value["constructors"])<=4,"bounded constructor observations")
        if configuration=="model": require(value["constructors"]==[],"model path performs no DSO loads")
        for row in value["constructors"]:
            require(type(row) is dict and set(row)=={"index","session","ordinal","raw_ra","owner","handle_present"},"constructor observation shape")
            require(all(uint(row[k]) for k in ("index","session","ordinal","raw_ra")) and row["index"]<4 and type(row["owner"]) is int and -1<=row["owner"]<4 and bit(row["handle_present"]),"constructor observation domains")
        require(type(value["modeled_generated"]) is list and len(value["modeled_generated"])==(4 if configuration=="model" else 0),"configuration-bound generated model observations")
        metadata_fields={"owner","binding","ncopy","out","target","replacement","original_slot","island","trampoline","continuation","target_exclusion","island_instructions","island_storage","trampoline_instructions","trampoline_storage","source_bytes"}
        span_fields={"target_exclusion","island_instructions","island_storage","trampoline_instructions","trampoline_storage"}
        for index,row in enumerate(value["modeled_generated"]):
            require(type(row) is dict and set(row)=={"slot","present","metadata"} and row["slot"]==index and bit(row["present"]),"model generated observation shape")
            m=row["metadata"]
            require(type(m) is dict and set(m)==metadata_fields,"closed generated metadata observation")
            require(type(m["owner"]) is dict and set(m["owner"])=={"kind","process_id","id"} and uint(m["owner"]["kind"]) and uint(m["owner"]["process_id"]) and type(m["owner"]["id"]) is str and len(m["owner"]["id"].encode())<=32,"copied generated owner shape")
            require(all(uint(m[k]) for k in metadata_fields-span_fields-{"owner","source_bytes"}),"generated numeric metadata domains")
            for field in span_fields: require(type(m[field]) is list and len(m[field])==2 and all(uint(x) for x in m[field]),"generated numeric span shape")
            require(type(m["source_bytes"]) is str and re.fullmatch(r"[0-9a-f]{32}",m["source_bytes"]),"generated source-word byte shape")
        require(value["consumer_return"] is None or (type(value["consumer_return"]) is str and len(value["consumer_return"].encode())<=32),"bounded returned permanent ID observation")

    def execute(self,variant,mode,selector,failures):
        self.phase="run-"+variant+"-"+mode+"-"+selector
        configuration=self.contract["selectors"][selector]["configuration"]
        key=variant+"-"+configuration+"-"+mode
        require(key in self.results["builds"],"reviewed retained build required")
        name="run-"+key+"-"+selector
        require(name not in {c["name"] for c in self.commands},"selector replay refused")
        folder=self.work/"evidence"/variant/configuration/mode/selector; folder.mkdir(parents=True)
        build=self.results["builds"][key]
        row={"variant":variant,"mode":mode,"configuration":configuration,"selector":selector,"returned":False,"accepted":False}
        self.attempts.append(row)
        rc,raw,err=self.docker(name,[build["binary"],selector,"/evidence/report.json","/build"],SAN_ENV if mode=="asan-ubsan" else {},runtime_build=self.work/"build"/"dsos"/mode,evidence=folder)
        row.update(returned=True,returncode=rc)
        checks=self.contract["selectors"][selector]["checks"]
        expected={"schema":1,"selector":selector,"checks":len(checks),"check_names":checks,"failures":len(failures),"failed_checks":failures,"target_invocations":0}
        value=decode(raw)
        require(value==expected and type(value["checks"]) is int and type(value["failures"]) is int,"exact full named outcome")
        require(raw==(json.dumps(expected,separators=(",",":"))+"\n").encode(),"canonical report bytes")
        require(rc==(1 if failures else 0) and err==b"","exact exit/no sanitizer diagnostics")
        inv=inventory(folder)
        require(set(inv["files"])=={"report.json"} and not inv["directories"],"one raw evidence file")
        report=regular(folder/"report.json"); require(0<len(report)<=MAX_REPORT,"bounded evidence")
        facts=decode(report)
        require(type(facts) is dict and set(facts)=={*expected,"main_lo","main_hi","lifecycle_status","dropped_claims","target_hits","replacement_hits","loader","module","calls","receipt","attribution"} and facts["schema"]==1,"closed evidence top-level schema")
        require(all(facts[k]==v for k,v in expected.items()),"raw/report check agreement")
        require(type(facts["loader"]) is dict and type(facts["module"]) is dict and type(facts["calls"]) is list and type(facts["receipt"]) is dict,"raw fact containers")
        require(len(facts["calls"])<=16 and facts["dropped_claims"]==0,"bounded lossless raw calls")
        for key in ("target_hits","replacement_hits"):
            require(type(facts[key]) is list and len(facts[key])==4 and all(type(x) is int and x==0 for x in facts[key]),"four zero actual invocation counters")
        require(facts["target_invocations"]==sum(facts["target_hits"])+sum(facts["replacement_hits"]),"computed invocation total")
        require(all(type(facts[k]) is int and 0<=facts[k]<(1<<64) for k in ("main_lo","main_hi")),"raw address scalar domains")
        require(type(facts["lifecycle_status"]) is int and 0<=facts["lifecycle_status"]<(1<<32),"raw serialized unsigned lifecycle result domain")
        self.attribution_shape(facts["attribution"],configuration)
        row["accepted"]=True
        # Selected module/receipt details stay private; allowlist only semantic names and byte pins.
        out={"report":expected,"returncode":rc,"evidence":metadata(report),"stdout":metadata(raw),"stderr":metadata(err),"configuration":configuration,"ordinary_builtin_ra_entry":configuration=="actual","patched_target_or_replacement_calls":0}
        bucket="candidate" if variant=="candidate" else "controls"
        self.results[bucket].setdefault(mode,{})[selector if bucket=="candidate" else variant]=out
        self.check_inputs()
        for b in self.results["builds"].values():
            for path,pin in b["artifacts"].items(): require(metadata(regular(self.work/path))==pin,"post-run immutable artifact")

    def run(self):
        modes=select(self.args.modes,("plain","asan-ubsan"),"modes")
        variants=select(self.args.variants,("candidate",*CONTROL_MACROS),"runtime variants")
        selectors=select(self.args.selectors,self.contract["selectors"],"selectors")
        configurations=select(self.args.configurations,("actual","model"),"configurations")
        selected=0
        for mode in modes:
            for variant in variants:
                if variant=="candidate":
                    for selector in selectors:
                        if self.contract["selectors"][selector]["configuration"] in configurations:
                            self.execute(variant,mode,selector,[]); selected+=1
                else:
                    c=self.contract["controls"][variant]
                    if c["configuration"] in configurations:
                        self.execute(variant,mode,c["selector"],c["expected_failures"]); selected+=1
        require(selected>0,"nonempty selected runtime")
        self.phase="complete-selected-runtime"

    def perform(self):
        if self.args.packet:
            require(not self.args.reuse_standalone,"standalone adoption belongs only to fresh preparation")
            self.resume()
        else:
            require(self.args.phase in ("prepare","all"),"new packet must prepare first")
            self.prepare()
        if self.args.phase in ("build","all"): self.build()
        if self.args.phase in ("run","all"): self.run()
        return {"schema":1,"status":"passed","phase":self.phase,"scope":"new immutable module and generated-range attribution","runtime_gate":"CLOSED","inputs":self.manifest,"results":self.results,"commands":self.commands,"execution_attempts":self.attempts,"old_matrix_or_composer_replay":False,"native_firmware_or_device_access":False}

def sanitized(result,private_hash):
    r=result.get("results",{})
    return {"schema":1,"status":result["status"],"runtime_gate":"CLOSED","scope":"new immutable module and generated-range attribution","private_result_sha256":private_hash,"inputs":result.get("inputs",{}),"candidate":r.get("candidate",{}),"controls":r.get("controls",{}),"builds":{k:{"kind":v["kind"],"mode":v["mode"],"configuration":v.get("configuration"),"compile_only":v.get("compile_only",False),"adopted":v.get("adopted",False),"artifacts":v["artifacts"]} for k,v in r.get("builds",{}).items()},"commands":[{"name":c["name"],"returncode":c["returncode"],"exception_present":c["exception"] is not None,"stdout":c["stdout"],"stderr":c["stderr"],"receipt_sha256":c["receipt_sha256"]} for c in result.get("commands",[])],"execution_attempts":result.get("execution_attempts",[]),"output_streams":2*len(result.get("commands",[])),"retained_files_including_private_result":result.get("retained_files_including_result"),"image":r.get("image"),"toolchain":r.get("toolchain"),"dso_reuse":r.get("dso_reuse"),"standalone_reuse":r.get("standalone_reuse"),"actual_builtin_ra_executed":any(x.get("configuration")=="actual" for bucket in ("candidate","controls") for rows in r.get(bucket,{}).values() for x in rows.values()),"RC_FIXTURE":False,"CC_FIXTURE":False,"DP_FIXTURE":False,"MA_FIXTURE":True,"actual_and_model_separate":True,"old_matrix_or_composer_replay":False,"native_firmware_or_device_access":False,"generated_code_invoked":False,"live_gate":"CLOSED"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--docker",default=shutil.which("docker") or "/opt/homebrew/bin/docker")
    p.add_argument("--phase",choices=("prepare","build","run","all"),default="prepare")
    p.add_argument("--configurations",help="actual,model; built as separate configurations")
    p.add_argument("--reuse-standalone",action="store_true",help="prepare only: adopt the two pinned successful standalone artifacts from the preserved guard-oracle failure")
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
