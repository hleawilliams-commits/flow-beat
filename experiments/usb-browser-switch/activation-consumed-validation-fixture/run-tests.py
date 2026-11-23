#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh consumed-validation C11 witness; source-frozen owned disposable files only."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused")
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import struct
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PARENT = HERE.parent / "activation-ordinary-file-fixture"
PLAN_DIR = HERE.parent / "activation-consumed-validation-plan"
PLAN_SHA = "0a76e2977f96c8aee82e2e9c12728fe53ee5dc4c9bb33d1666349a2fe4301c06"
README_SHA = "e4d2a047574dd4751fc2647cc072b2f05d73063894e3a439b10fb783ba99b116"
SCAFFOLD_SHA = "323f5526f36aece7f6ecc87f1e8d32d72061e817bf7527923285ff20390eaefe"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SOURCE_NAMES = ("run-tests.py", "core.c", "core.h", "source-transform.json",
                "file_provider.h", "file_provider.c", "native_leaf.h", "native_leaf.c", "test_file.c", "test-contract.json",
                "native-layout-spec.json")
METHOD_NAMES = ("ac_run", "ac_observe", "ac_admit_internal", "ac_publish_internal", "ac_receive",
                "ac_select_values", "ma_module_snapshot", "ma_generated_index", "ma_owner_index", "rc_lookup")
SETUP_NAMES = ("ac_model_stage_module", "ac_model_commit_module", "ac_model_stage_generated",
               "ac_model_commit_generated", "ac_model_add_cell")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-fno-pie"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"}
CONTROLS = {'corrupt-query-export': ('CV_CONTROL_CORRUPT_QUERY_EXPORT', 'control-query-evidence', 'validation-veto-preserves-native-query-evidence'), 'normalize-refusal': ('CV_CONTROL_NORMALIZE_REFUSAL', 'control-refusal-domain', 'validation-veto-remains-terminal-incomplete'), 'omit-held-cleanup': ('CV_CONTROL_OMIT_HELD_CLEANUP', 'control-held-cleanup', 'held-validation-refusal-has-explicit-cleanup')}
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault|FIXTURE_ABORT|warning:|error:", re.I)
MAX_REPORT = 1024*1024
SCOPE = "Offline C11 consumed-validation witness with authored direct ownership and owned disposable files"
EXCLUDED = ("checkpoint.json", "execution.lock")
class Refusal(RuntimeError):
    pass
def require(ok, message):
    if not ok: raise Refusal(message)
def digest(raw): return hashlib.sha256(raw).hexdigest()
def metadata(raw): return {"bytes":len(raw),"sha256":digest(raw)}
def encoded(value): return (json.dumps(value,indent=2,sort_keys=True)+"\n").encode()
def save(path,value):
    with path.open("xb") as stream: stream.write(encoded(value))
def regular(path):
    require(path.is_file() and not path.is_symlink(),"regular input: "+str(path))
    before=path.stat(); raw=path.read_bytes(); after=path.stat()
    require(all(getattr(before,k)==getattr(after,k) for k in ("st_dev","st_ino","st_size","st_mtime_ns","st_ctime_ns")) and len(raw)==before.st_size,"stable input")
    return raw
def no_duplicates(pairs):
    out={}
    for k,v in pairs:
        require(k not in out,"duplicate JSON key"); out[k]=v
    return out
def decode(raw):
    return json.loads(raw,object_pairs_hook=no_duplicates,
                      parse_constant=lambda _: (_ for _ in ()).throw(Refusal("nonfinite JSON")))
def relative(name):
    require(type(name) is str and re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*",name) is not None and all(x not in (".","..") for x in name.split("/")),"safe relative path")
    return name
def inventory(root,excludes=()):
    files={}; dirs=[]
    for p in sorted(root.rglob("*")):
        require(not p.is_symlink(),"no packet symlink")
        name=relative(p.relative_to(root).as_posix())
        if name in excludes: continue
        if p.is_file(): files[name]=metadata(regular(p))
        elif p.is_dir(): dirs.append(name)
        else: raise Refusal("nonregular packet path")
    return {"files":files,"directories":dirs}

class Elf:
    """Bounded ELF64 metadata only; no invocation or disassembler imports."""
    def __init__(self, raw, kind):
        require(len(raw) >= 64 and raw[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little-endian identity")
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", raw)
        require(h[1] == kind and h[2] == 183 and h[3] == 1 and h[8] == 64, "AArch64 ELF type/header")
        off, width, count, names_index = h[6], h[11], h[12], h[13]
        require(width == 64 and 0 < count < 65535 and names_index < count
                and off + width * count <= len(raw), "complete ordinary ELF section table")
        sections = []
        for i in range(count):
            row = struct.unpack_from("<IIQQQQIIQQ", raw, off + i * width)
            s = dict(zip(("name_offset", "type", "flags", "address", "offset", "size",
                          "link", "info", "alignment", "entry_size"), row))
            require(s["type"] == 8 or s["offset"] + s["size"] <= len(raw), "section bounds")
            s["data"] = b"" if s["type"] == 8 else raw[s["offset"]:s["offset"] + s["size"]]
            sections.append(s)
        names = sections[names_index]["data"]
        for s in sections:
            s["name"] = self.string(names, s["name_offset"])
        self.symbols = []
        tables = 0
        for s in sections:
            if s["type"] != 2:
                continue
            tables += 1
            require(s["entry_size"] == 24 and s["size"] % 24 == 0 and s["link"] < count, "symbol table bounds")
            strings = sections[s["link"]]["data"]
            for at in range(0, s["size"], 24):
                name, info, other, index, address, size = struct.unpack_from("<IBBHQQ", s["data"], at)
                self.symbols.append({"name": self.string(strings, name), "bind": info >> 4,
                                     "type": info & 15, "other": other, "section": index,
                                     "value": address, "size": size})
        require(tables == 1, "one regular symbol table")
        self.sections = sections
        self.kind = kind

    @staticmethod
    def string(raw, at):
        require(at < len(raw), "ELF string offset")
        end = raw.find(b"\0", at)
        require(end >= at, "ELF string terminator")
        return raw[at:end].decode("utf-8", "strict")

    def function(self, name):
        rows = [s for s in self.symbols if s["name"] == name and 0 < s["section"] < len(self.sections)]
        require(len(rows) == 1 and rows[0]["type"] == 2 and rows[0]["size"] > 0, "defined function: " + name)
        row = rows[0]
        section = self.sections[row["section"]]
        require(section["flags"] & 6 == 6 and section["address"] <= row["value"]
                and row["value"] + row["size"] <= section["address"] + section["size"], "allocated executable function extent")
        return row

    def extent(self, name):
        row = self.function(name)
        section = self.sections[row["section"]]
        offset = row["value"] - section["address"]
        body = section["data"][offset:offset + row["size"]]
        require(len(body) == row["size"], "complete method bytes")
        return {"section": row["section"], "value": row["value"], "size": row["size"],
                "body_sha256": digest(body)}

    def summary(self):
        return {"type": self.kind, "machine": "AArch64", "elf_class": 64, "little_endian": True,
                "sections": [{k: v for k, v in s.items() if k != "data"} for s in self.sections],
                "symbols": self.symbols}

class Run:
    def __init__(self,args):
        self.args=args
        self.token=uuid.uuid4().hex
        self.docker_path=str(Path(args.docker).resolve())
        self.path=None; self.owned=False; self.lock_owned=False
        self.inputs=None; self.work=None; self.manifest={}
        self.commands=[]; self.attempts=[]; self.phase="initial"
        self.results={"builds":{},"candidate":{},"controls":{}}
    def acquire(self):
        private=HERE/"private"; private.mkdir(exist_ok=True)
        require(not private.is_symlink(),"owned private directory")
        if self.args.packet:
            require(re.fullmatch(r"run-[0-9a-f]{32}",self.args.packet) is not None,"bounded packet name")
            self.path=private/self.args.packet
            require(self.path.is_dir() and not self.path.is_symlink(),"existing owned packet")
        else:
            self.path=private/("run-"+uuid.uuid4().hex); self.path.mkdir()
        self.inputs=self.path/"inputs"; self.work=self.path/"work"
        with (self.path/"execution.lock").open("x") as stream: stream.write(self.token)
        self.lock_owned=True; self.owned=True
        if self.args.packet: self.resume()
        else: self.prepare()
    def put(self,name,raw,origin):
        name=relative(name); require(name not in self.manifest,"unique retained source")
        p=self.inputs/name; p.parent.mkdir(parents=True,exist_ok=True)
        with p.open("xb") as stream: stream.write(raw)
        self.manifest[name]={**metadata(raw),"origin":str(origin)}
    def check_inputs(self):
        actual=inventory(self.inputs)["files"]
        require(actual=={n:{k:v[k] for k in ("bytes","sha256")} for n,v in self.manifest.items()},"closed immutable retained inputs")
        for n,v in self.manifest.items():
            require(metadata(regular(Path(v["origin"])))==actual[n],"source changed since freeze: "+n)
    def prepare(self):
        self.phase="prepare"
        self.inputs.mkdir(); self.work.mkdir(); (self.path/"commands").mkdir()
        save(self.path/"started.json",{"schema":1,"image":IMAGE_ID,"scope":SCOPE,"runtime_gate":"CLOSED","python":sys.version})
        lockraw=regular(HERE/"review-lock.json"); lock=decode(lockraw)
        require(set(lock)=={"schema","status","files"} and lock["schema"]==1 and lock["status"]=="reviewed-before-build","review lock")
        require(set(lock["files"])==set(SOURCE_NAMES)|{"source-ledger.json"},"exact lock source set")
        for name in SOURCE_NAMES+("source-ledger.json",):
            raw=regular(HERE/name); require(metadata(raw)==lock["files"][name],"reviewed source pin: "+name)
            self.put("new/"+name,raw,HERE/name)
        self.put("review-lock.json",lockraw,HERE/"review-lock.json")
        ledger=decode(regular(HERE/"source-ledger.json"))
        require(ledger["schema"]==1 and ledger["runtime_gate"]=="CLOSED" and ledger["source_files"]=={n:lock["files"][n] for n in SOURCE_NAMES},"exact source ledger")
        reviewraw=regular(HERE/"source-review.json"); review=decode(reviewraw)
        require(review["status"]=="passed" and review["runtime_gate"]=="CLOSED" and review["review_lock"]==metadata(lockraw),"independent prebuild source/control review")
        self.put("source-review.json",reviewraw,HERE/"source-review.json")
        planraw=regular(PLAN_DIR/"consumed-validation-plan.json")
        require(digest(planraw)==PLAN_SHA and digest(regular(PLAN_DIR/"README.md"))==README_SHA,"retained consumed-validation plan pins")
        plan=decode(planraw)
        for name in ("consumed-validation-plan.json","README.md","result.json","verification.json"):
            self.put("plan/"+name,regular(PLAN_DIR/name),PLAN_DIR/name)
        for spec in plan["sources"]:
            raw=regular(REPO/relative(spec["path"])); require(metadata(raw)=={k:spec[k] for k in ("bytes","sha256")},"retained frozen source")
            self.put("frozen/"+spec["name"],raw,REPO/spec["path"])
        scaffold=regular(PARENT/"run-tests.py");require(digest(scaffold)==SCAFFOLD_SHA,"fixed evidence scaffold")
        self.put("reference/predecessor-runner.py",scaffold,PARENT/"run-tests.py")
        contract=decode(regular(self.inputs/"new/test-contract.json"))
        self.contract=contract
        self.check_contract()
        transform=decode(regular(self.inputs/"new/source-transform.json"))
        require(transform["schema"]==1 and transform["runtime_gate"]=="CLOSED"
            and transform["status"]=="source-written-not-built"
            and set(transform["files"])=={"core.c","core.h","native_leaf.c","native_leaf.h"},"exact unchanged engine and leaf custody")
        require(transform["plan"]=={"path":(PLAN_DIR/"consumed-validation-plan.json").relative_to(REPO).as_posix(),
                **metadata(planraw)},"ledger binds retained consumed-validation plan")
        require(transform["reader_inputs"]==[x for x in plan["sources"] if x["name"].startswith("generated/")],
                "ledger binds exact retained reader/type fragments")
        require(transform["source_groups"]==plan["source_groups"],"all seven planned source groups retained")
        for name,spec in transform["files"].items():
            frozen=regular(self.inputs/("frozen/witness/"+name))
            new=regular(self.inputs/("new/"+name))
            require(frozen==new and spec=={"mode":"unchanged","pin":metadata(frozen)},"byte-identical engine/header/native-leaf pair")
        generated=self.work/"generated"; generated.mkdir()
        for name in ("readers.inc","receipt-types.h"):
            with (generated/name).open("xb") as stream:
                stream.write(regular(self.inputs/("frozen/generated/"+name)))
        self.results["reader_reuse"]={"artifacts":inventory(generated)["files"],"extractor_executed":False,
                                     "scaffold":metadata(scaffold)}
        self.check_inputs()
        self.refresh_artifacts()
        self.phase="prepared"
    def check_contract(self):
        c=self.contract
        require(c["schema"]==1 and c["status"]=="source-frozen-not-executed"
                and c["configuration"]=="AO_WITNESS_SETUP","consumed-validation contract")
        require(c["summary_keys"]==["schema","selector","checks","check_names","failures","failed_checks"]
                and len(c["selectors"])==4 and set(c["controls"])==set(CONTROLS),
                "exact fresh selector/control scope")
        plan=decode(regular(self.inputs/"plan/consumed-validation-plan.json"))
        matrix=plan["future_matrix"]
        normals=matrix["ordinary_selectors"]
        require(normals==["consumed-validation-veto"] and matrix["total_future_exec_children"]==14,
                "one ordinary selector and14 fresh children")
        require(set(c["selectors"])==set(normals)|set(matrix["baseline_selectors"]),
                "exact planned normal and baseline selectors")
        assertions=[x["id"] for x in plan["oracle_partition"]["shared"]+plan["oracle_partition"]["composites"]]
        require(len(assertions)==8 and len(set(assertions))==8,"five shared and three composites")
        for name,row in c["selectors"].items():
            relative(name)
            require(row["checks"]==assertions,"identical eight assertions for every witness execution")
        for name,(macro,selector,failure) in CONTROLS.items():
            v=c["controls"][name]
            require(v["macro"]==macro and v["selector"]==selector
                    and v["expected_failures"]==[failure] and failure in c["selectors"][selector]["checks"],
                    "exact singleton control failure set")
        require(c["summary_counts"]=={"normal_selectors":1,"control_baselines":3,
                "candidate_selectors_per_mode":4,"controls_per_mode":3,"fresh_witness_children":14,
                "supervisor_invocations":14,"summed_assertions_per_mode":32,"distinct_assertion_names":8},
                "exact predeclared counts distinguish witness children from supervisors")
        require(c["authored_inputs"]==matrix["authored_inputs"],"exact authored direct owner/request values")
        specraw=regular(self.inputs/"new/native-layout-spec.json")
        spec=decode(specraw)
        schema=c["evidence_schema"]
        require(schema["native_layout_spec"]==metadata(specraw)
                and spec["schema"]==1 and spec["envelope"]["version"]==2
                and spec["envelope"]["summary_schema"]==1
                and spec["envelope"]["pointer_free"] is True
                and spec["envelope"]["same_build_only"] is True,
                "frozen native layout specification and new envelope")
        require(schema["summary_schema"]==1 and schema["child_report_schema"]==2
                and schema["binary_length_key"] in schema["binary_layout_keys"]
                and len(schema["binary_layout_keys"])==len(set(schema["binary_layout_keys"]))
                and 12<=len(schema["binary_layout_keys"])<=40,
                "new measured report ABI contract")
    def resume(self):
        pointer=decode(regular(self.path/"checkpoint.json"))
        raw=regular(self.path/relative(pointer["result"]))
        require(metadata(raw)==pointer["result_pin"],"checkpoint result pin")
        result=decode(raw)
        require(result["status"]=="passed" and result["phase"]=="built-preflight","resume only first preflight success")
        projection=regular(self.path/relative(pointer["sanitized"]))
        require(metadata(projection)==pointer["sanitized_pin"] and decode(projection)==sanitized(result,digest(raw)),"checkpoint projection")
        expected=dict(result["retained_inventory_before_result"]["files"])
        expected[pointer["result"]]=metadata(raw);expected[pointer["sanitized"]]=metadata(projection)
        require(inventory(self.path,EXCLUDED)=={"files":expected,"directories":result["retained_inventory_before_result"]["directories"]},"unchanged complete preflight packet")
        self.manifest=result["inputs"];self.results=result["results"];self.commands=result["commands"];self.attempts=result["execution_attempts"]
        self.contract=decode(regular(self.inputs/"new/test-contract.json"));self.check_contract()
        self.check_inputs();self.check_artifacts();self.phase="resumed-preflight"
    def check_artifacts(self):
        if "artifact_inventory" in self.results:
            require(inventory(self.work)==self.results["artifact_inventory"],"unchanged owned build/report artifacts")
    def refresh_artifacts(self):
        current=inventory(self.work)
        previous=self.results.get("artifact_inventory",{"files":{},"directories":[]})
        require(all(current["files"].get(n)==pin for n,pin in previous["files"].items())
            and set(previous["directories"]).issubset(current["directories"]),"existing artifact custody before additive refresh")
        self.results["artifact_inventory"]=current
    def command(self,name,argv,container=None):
        require(not any(c["name"]==name for c in self.commands),"successful command replay refused")
        folder=self.path/"commands"/("%04d-"%(len(self.commands)+1)+relative(name));folder.mkdir()
        environment={"PATH":os.environ.get("PATH","/usr/bin:/bin"),"HOME":os.environ.get("HOME",""),"LANG":"C","LC_ALL":"C"}
        for key in ("DOCKER_HOST","DOCKER_CONTEXT","DOCKER_CONFIG"):
            if key in os.environ: environment[key]=os.environ[key]
        spec={"argv":list(argv),"cwd":str(self.path),"environment":environment,"timeout_seconds":45,"container":container}
        save(folder/"command.json",spec)
        proc=None;out=b"";err=b"";exception=None;cleanup={}
        try:
            proc=subprocess.Popen(argv,cwd=self.path,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
            out,err=proc.communicate(timeout=45)
        except BaseException as exc:
            exception=repr(exc)
            if isinstance(exc,subprocess.TimeoutExpired):
                out=exc.stdout or b"";err=exc.stderr or b""
            if proc is not None:
                try:os.killpg(proc.pid,signal.SIGKILL)
                except ProcessLookupError:cleanup["process_already_exited"]=True
                except BaseException as kill_error:cleanup["kill_error"]=repr(kill_error)
                try:
                    drained_out,drained_err=proc.communicate(timeout=5)
                    out,err=drained_out,drained_err
                except BaseException as drain:
                    cleanup["drain_error"]=repr(drain)
                    if isinstance(drain,subprocess.TimeoutExpired):
                        out=drain.stdout if drain.stdout is not None else out
                        err=drain.stderr if drain.stderr is not None else err
            if container:
                try:
                    cp=subprocess.run([self.docker_path,"rm","-f",container],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15,env=environment)
                    (folder/"cleanup-stdout.bin").write_bytes(cp.stdout);(folder/"cleanup-stderr.bin").write_bytes(cp.stderr)
                    cleanup.update(returncode=cp.returncode,stdout=metadata(cp.stdout),stderr=metadata(cp.stderr))
                except BaseException as ce:cleanup["container_error"]=repr(ce)
        (folder/"stdout.bin").write_bytes(out);(folder/"stderr.bin").write_bytes(err)
        receipt={**spec,"name":name,"directory":folder.name,"returncode":None if proc is None else proc.returncode,
                 "exception":exception,"cleanup":cleanup,"stdout":metadata(out),"stderr":metadata(err)}
        save(folder/"receipt.json",receipt);self.commands.append({**receipt,"receipt_sha256":digest(encoded(receipt))})
        require(exception is None,"owned command exception: "+name)
        return receipt["returncode"],out,err
    def docker(self,name,action,mode=None,evidence=None):
        container="az-consumed-validation-"+self.path.name[4:]+"-"+str(len(self.commands)+1)
        env={"PATH":"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin","LANG":"C","LC_ALL":"C"}
        if mode=="asan-ubsan":env.update(SAN_ENV)
        argv=[self.docker_path,"run","--name",container,"--rm","--pull=never","--network=none","--platform=linux/arm64","--read-only",
              "--tmpfs","/tmp:rw,nosuid,nodev","--workdir","/out",
              "--mount","type=bind,source="+str(self.inputs)+",target=/src,readonly",
              "--mount","type=bind,source="+str(self.work)+",target=/out"+(",readonly" if evidence else "")]
        if evidence is not None:argv+=["--mount","type=bind,source="+str(evidence)+",target=/evidence"]
        argv += [IMAGE_ID,"env","-i"]+[k+"="+v for k,v in env.items()]+list(action)
        return self.command(name,argv,container)
    def clean(self,name,action,host=False,empty=False):
        rc,out,err=self.command(name,action) if host else self.docker(name,action)
        require(rc==0 and err==b"" and not DIAGNOSTIC.search(out.decode("utf-8","replace")),"clean command: "+name)
        if empty:require(out==b"","empty compiler/link output")
        return out
    def flags(self,mode,setup=False,control=None):
        return COMMON+(SAN_FLAGS if mode=="asan-ubsan" else [])+(["-DAO_WITNESS_SETUP=1"] if setup else [])+(["-D"+CONTROLS[control][0]+"=1"] if control else [])
    def inspect_toolchain(self):
        image=decode(self.clean("image-inspect",[self.docker_path,"image","inspect",IMAGE_ID],host=True))
        require(type(image) is list and len(image)==1 and image[0]["Id"]==IMAGE_ID
                and image[0]["Architecture"]=="arm64" and image[0]["Os"]=="linux","pinned offline image")
        version=self.clean("compiler-version",["gcc","--version"])
        target=self.clean("compiler-target",["gcc","-dumpmachine"])
        require(b"7.5.0" in version.splitlines()[0] and target.strip()==b"aarch64-linux-gnu","GCC 7.5 AArch64")
        provenance=self.work/"provenance"; provenance.mkdir()
        self.results["image"]={k:image[0][k] for k in ("Id","Architecture","Os")}
        self.results["toolchain"]={"version":metadata(version),"target":target.decode().strip()}
        for name,argv in (
            ("libc-version",["getconf","GNU_LIBC_VERSION"]),
            ("libc-loader-sha256",["sha256sum","/lib/aarch64-linux-gnu/libc.so.6","/lib/ld-linux-aarch64.so.1"]),
            ("compiler-config",["gcc","-v"])):
            rc,out,err=self.docker(name,argv)
            # gcc -v deliberately prints its configuration on stderr; every byte is retained.
            if name=="compiler-config":
                require(rc==0 and out==b"" and b"gcc version 7.5.0" in err
                        and not DIAGNOSTIC.search(err.decode("utf-8","replace")),"compiler configuration only")
            else:
                require(rc==0 and err==b"" and not DIAGNOSTIC.search(out.decode("utf-8","replace")),"native provenance command")
            (provenance/(name+"-stdout.txt")).write_bytes(out)
            (provenance/(name+"-stderr.txt")).write_bytes(err)
        # Preprocessing retains the actual included ABI/header text without executing native leaves.
        for name,source,options in (("native-leaf-expanded","native_leaf.c",["-E"]),
            ("native-leaf-macros","native_leaf.c",["-E","-dM"]),("test-file-expanded","test_file.c",["-E"])):
            rc,out,err=self.docker(name,["gcc",*COMMON,"-DAO_WITNESS_SETUP=1","-I/src/new","-I/out/generated",
                *options,"/src/new/"+source])
            require(rc==0 and err==b"","native header preprocessing diagnostics")
            (provenance/(name+".txt")).write_bytes(out)
        self.results["native_provenance"]={"artifacts":inventory(provenance)["files"],
            "scope":"Pinned-image ordinary libc/header/ABI inputs only; no transitive or signal-safety proof"}

    def review_object(self,key,folder,stem,setup=False):
        raw=regular(folder/(stem+".o")); elf=Elf(raw,1)
        present={x["name"] for x in elf.symbols if x["section"]}
        if stem=="core":
            require(not any(n.startswith("ac_fixture_") or n in ("ac_model_diagnostic","ac_aux","ac_snapshot_refused")
                            for n in present),"private fixture symbols absent")
            require({n for n in present if n.startswith("ac_model_")}==(set(SETUP_NAMES) if setup else set()),
                    "exact five-function setup-only symbol surface")
            for n in METHOD_NAMES+(SETUP_NAMES if setup else ()):elf.function(n)
            names=list(METHOD_NAMES+(SETUP_NAMES if setup else ()))
        else:
            names=sorted({x["name"] for x in elf.symbols if x["type"]==2
                          and 0<x["section"]<len(elf.sections) and x["size"]>0
                          and elf.sections[x["section"]]["flags"]&6==6})
            require(0<len(names)<=256,"bounded new-source function inventory")
            for n in names:elf.function(n)
        target="/out/"+folder.relative_to(self.work).as_posix()+"/"+stem+".o"
        dis=self.clean(key+"-"+stem+"-disassembly",["objdump","-dr",target])
        require(regular(folder/(stem+".o"))==raw,"metadata preserves object")
        (folder/(stem+"-disassembly.txt")).write_bytes(dis)
        save(folder/(stem+"-methods.json"),{n:elf.extent(n) for n in names})
        save(folder/(stem+"-symbols.json"),elf.summary())
        save(folder/(stem+"-undefined.json"),{"undefined_symbols":[x for x in elf.symbols if not x["section"] and x["name"]],
            "scope":"Direct dependencies only, including instrumentation/canary/libc; no transitive or signal proof"})
        return elf

    def compile_object(self,key,folder,stem,mode,setup=False,control=None):
        target="/out/"+folder.relative_to(self.work).as_posix()
        flags=self.flags(mode,setup,control)
        self.clean("compile-"+key+"-"+stem,["gcc",*flags,"-I/src/new","-I/out/generated",
            "-c","/src/new/"+stem+".c","-o",target+"/"+stem+".o"],empty=True)
        self.review_object(key,folder,stem,setup)
    def preflight(self):
        require(not self.args.packet and self.args.phase=="build","first owned preflight only")
        self.inspect_toolchain()
        for mode in ("plain","asan-ubsan"):
            key="standalone-"+mode; folder=self.work/"build"/"standalone"/mode;folder.mkdir(parents=True)
            self.compile_object(key,folder,"core",mode)
            save(folder/"configuration.json",{"flags":self.flags(mode),"AO_WITNESS_SETUP":False,"compile_only":True})
            self.results["builds"][key]={"variant":"standalone","mode":mode,"kind":"preflight","compile_only":True,
                "objects":["core"],"reuse":{},"artifacts":inventory(folder)["files"]}
            for variant in ("candidate",*CONTROLS): self.executable(variant,mode)
        self.refresh_artifacts();self.check_inputs();self.phase="built-preflight"
    def executable(self,variant,mode):
        key=variant+"-"+mode;require(key not in self.results["builds"],"successful build replay refused")
        folder=self.work/"build"/variant/mode;folder.mkdir(parents=True)
        target="/out/"+folder.relative_to(self.work).as_posix()
        control=None if variant=="candidate" else variant
        reuse={}; reviewed=[]
        for stem in ("core","native_leaf","file_provider","test_file"):
            if control is not None and stem!="file_provider":
                source=self.work/"build"/"candidate"/mode/(stem+".o");raw=regular(source)
                with (folder/(stem+".o")).open("xb") as stream:stream.write(raw)
                reuse[stem+".o"]={"source":source.relative_to(self.work).as_posix(),**metadata(raw)}
            else:
                self.compile_object(key,folder,stem,mode,True,control if stem=="file_provider" else None)
                reviewed.append(stem)
        flags=self.flags(mode,True)
        self.clean("link-"+key,["gcc",*flags,"-no-pie",*[target+"/"+x+".o" for x in
            ("core","native_leaf","file_provider","test_file")],"-o",target+"/test-file"],empty=True)
        raw=regular(folder/"test-file"); elf=Elf(raw,2);elf.function("ac_run");elf.function("ac_observe")
        present={x["name"] for x in elf.symbols if x["section"]}
        require({n for n in present if n.startswith("ac_model_")}==set(SETUP_NAMES)
                and not any(n.startswith("ac_fixture_") for n in present),"linked setup-only surface")
        save(folder/"linked-symbols.json",elf.summary())
        (folder/"linked-disassembly.txt").write_bytes(self.clean(key+"-linked-disassembly",["objdump","-d",target+"/test-file"]))
        require(regular(folder/"test-file")==raw,"disassembler preserves executable")
        save(folder/"configuration.json",{"flags":flags,"AO_WITNESS_SETUP":True,"compile_only":False,
            "not_yet_invoked":True,"control":control,"provider_macro":None if control is None else CONTROLS[control][0],
            "same_packet_artifact_reuse":reuse})
        self.results["builds"][key]={"variant":variant,"mode":mode,"kind":"preflight","compile_only":False,
            "objects":reviewed,"reuse":reuse,"binary":target+"/test-file","artifacts":inventory(folder)["files"]}
        self.refresh_artifacts()
    def adopt_review(self):
        self.check_inputs();self.check_artifacts()
        raw=regular(Path(self.args.codegen_review));v=decode(raw)
        expected={k:x["artifacts"] for k,x in self.results["builds"].items()}
        require(set(v)=={"schema","status","phase","runtime_gate","review_lock","generated","preflight",
                        "method_reviews","dependency_review","limitations","native_layout"}
                and v["schema"]==1 and v["status"]=="reviewed-before-consumed-validation-runtime"
                and v["phase"]=="built-preflight" and v["runtime_gate"]=="CLOSED","fresh reviewed preflight schema")
        require(v["review_lock"]==metadata(regular(self.inputs/"review-lock.json"))
                and v["generated"]==self.results["reader_reuse"]["artifacts"] and v["preflight"]==expected,
                "review binds complete fresh preflight artifact sets")
        require(set(v["method_reviews"])==set(expected),"review every configuration")
        for key,b in self.results["builds"].items():
            rows=v["method_reviews"][key]
            require(set(rows)==set(b["objects"]),"review all independently compiled objects")
            folder=self.work/"build"/b["variant"]/b["mode"]
            for stem,methods_review in rows.items():
                methods=decode(regular(folder/(stem+"-methods.json")))
                require(set(methods_review)==set(methods),"review each selected method")
                for name,row in methods_review.items():
                    require(set(row)=={"extent","observations"} and row["extent"]==methods[name]
                            and type(row["observations"]) is list and row["observations"]
                            and all(type(x) is str and x for x in row["observations"]),"method-bound source/codegen review")
        require(type(v["dependency_review"]) is str and v["dependency_review"]
                and type(v["limitations"]) is list and v["limitations"],"native/dependency scope retained")
        layout=v["native_layout"]
        require(set(layout)=={"status","spec_pin","object_inputs","header_inputs","by_mode","observations","limitations"}
                and layout["status"]=="reviewed-before-runtime","fresh native ABI review")
        require(layout["spec_pin"]==metadata(regular(self.inputs/"new/native-layout-spec.json")),
                "native ABI review binds frozen layout specification")
        require(layout["object_inputs"]=={mode:metadata(regular(self.work/"build"/"candidate"/mode/"test_file.o"))
                for mode in ("plain","asan-ubsan")}
                and layout["header_inputs"]==self.results["native_provenance"]["artifacts"],
                "native ABI review binds actual new target objects and headers")
        keys=self.contract["evidence_schema"]["binary_layout_keys"]
        require(set(layout["by_mode"])=={"plain","asan-ubsan"},"ABI review both modes")
        for mode,values in layout["by_mode"].items():
            require(set(values)==set(keys)
                    and all(type(x) is int and 0<=x<=MAX_REPORT for x in values.values())
                    and all(value>0 for key,value in values.items() if key.endswith("_bytes"))
                    and values[self.contract["evidence_schema"]["binary_length_key"]]>0,
                    "closed measured ABI with positive sizes and nonnegative offsets")
        require(layout["by_mode"]["plain"]==layout["by_mode"]["asan-ubsan"],
                "same pointer-free report layout across modes")
        require(type(layout["observations"]) is list and layout["observations"]
                and all(type(x) is str and x for x in layout["observations"])
                and type(layout["limitations"]) is list and layout["limitations"],
                "native ABI interpretation and limits retained")
        folder=self.work/"review";folder.mkdir()
        with (folder/"codegen-review.json").open("xb") as stream:stream.write(raw)
        self.results["codegen_review"]={"artifact":metadata(raw),"runtime_gate":"CLOSED",
            "native_layout":layout["by_mode"]["plain"]}
        self.refresh_artifacts()
    def run_case(self,variant,mode,selector):
        name="run-"+variant+"-"+mode+"-"+selector
        require(not any(x["name"]==name for x in self.attempts),"no successful process replay")
        evidence=self.work/"evidence"/variant/mode/selector;evidence.mkdir(parents=True)
        self.attempts.append({"name":name,"variant":variant,"mode":mode,"selector":selector})
        binary=self.results["builds"][variant+"-"+mode]["binary"]
        rc,out,err=self.docker(name,[binary,selector,"/evidence/report.json"],mode,evidence)
        require(err==b"" and not DIAGNOSTIC.search(out.decode("utf-8","replace")),"runtime diagnostics refused")
        summary=decode(out)
        require(set(summary)==set(self.contract["summary_keys"]) and summary["schema"]==1 and summary["selector"]==selector,"exact runtime summary schema")
        expected=self.contract["selectors"][selector]["checks"]
        require(summary["check_names"]==expected and type(summary["checks"]) is int and summary["checks"]==len(expected) and type(summary["failures"]) is int,"exact named assertion sequence")
        failures=[] if variant=="candidate" else [CONTROLS[variant][2]]
        require(summary["failed_checks"]==failures and summary["failures"]==len(failures) and rc==(0 if not failures else 1),"exact safe-control failure set and exit")
        require(set(p.name for p in evidence.iterdir())=={"report.json","report.json.child.bin"},"closed owned report pair")
        raw=regular(evidence/"report.json");require(0<len(raw)<=MAX_REPORT,"bounded report")
        report=decode(raw);require({k:report[k] for k in self.contract["summary_keys"]}==summary,"public/private report agreement")
        self.report_shape(report)
        child=regular(evidence/"report.json.child.bin")
        require(len(child)==self.results["codegen_review"]["native_layout"][self.contract["evidence_schema"]["binary_length_key"]],
                "retained binary length matches independently measured target ABI")
        bucket=self.results["candidate"] if variant=="candidate" else self.results["controls"]
        bucket.setdefault(mode,{})[selector if variant=="candidate" else variant]={"selector":selector,"summary":summary,"report":metadata(raw),
            "command":name,"returncode":rc,"stdout":metadata(out),"stderr":metadata(err),"report_path":(evidence/"report.json").relative_to(self.work).as_posix(),
            "child_report":metadata(child),"child_report_path":(evidence/"report.json.child.bin").relative_to(self.work).as_posix()}
        self.refresh_artifacts()
    def run(self):
        require(self.args.packet and self.args.codegen_review,"reviewed preflight required")
        self.adopt_review()
        for mode in ("plain","asan-ubsan"):
            for selector in self.contract["selectors"]:self.run_case("candidate",mode,selector)
            for variant,(_,selector,_) in CONTROLS.items():self.run_case(variant,mode,selector)
        self.check_inputs();self.check_artifacts();self.phase="completed-new-consumed-validation-matrix"
    def report_shape(self,report):
        # Root-reviewed C parent supplies semantic assertions. Structural closure and
        # bounded report size here never convert supplied callback values into native outcomes.
        schema=self.contract["evidence_schema"]
        require(type(report) is dict and set(report)==set(schema["private_keys"]),"closed private report")
        require(report["source_scope"]==schema["source_scope"],"declared consumed-validation owned-file scope")
        require(report["binary_layout"]==self.results["codegen_review"]["native_layout"],
                "runtime report matches reviewed target ABI")
        def bounded(value,depth=0):
            require(depth<=12,"bounded report nesting")
            if value is None or type(value) in (bool,int):return
            if type(value) is str:
                require(len(value)<=4096,"bounded report string");return
            if type(value) is list:
                require(len(value)<=256,"bounded report array")
                for x in value:bounded(x,depth+1)
                return
            require(type(value) is dict and len(value)<=256
                    and all(type(k) is str and len(k)<=128 for k in value),"bounded value object")
            for x in value.values():bounded(x,depth+1)
        bounded(report)
    def perform(self):
        self.acquire()
        if self.args.phase=="build":self.preflight()
        elif self.args.phase=="run":self.run()
        else:raise Refusal("unknown execution phase")
        return {"schema":1,"status":"passed","phase":self.phase,"scope":SCOPE,"runtime_gate":"CLOSED",
                "inputs":self.manifest,"results":self.results,"commands":self.commands,"execution_attempts":self.attempts,
                "old_capture_or_matrix_replay":False,"native_firmware_or_device_access":False}

def sanitized(result,private_hash):
    r=result.get("results",{})
    return {"schema":1,"status":result["status"],"phase":result["phase"],"scope":SCOPE,"runtime_gate":"CLOSED",
            "private_result_sha256":private_hash,
            "inputs":{n:{k:v[k] for k in ("bytes","sha256")} for n,v in result.get("inputs",{}).items()},
            "builds":r.get("builds",{}),"candidate":r.get("candidate",{}),"controls":r.get("controls",{}),
            "commands":[{"name":c["name"],"returncode":c["returncode"],"exception_present":c["exception"] is not None,
                         "stdout":c["stdout"],"stderr":c["stderr"],"receipt_sha256":c["receipt_sha256"]} for c in result.get("commands",[])],
            "execution_attempts":result.get("execution_attempts",[]),"output_streams":2*len(result.get("commands",[])),
            "image":r.get("image"),"toolchain":r.get("toolchain"),"reader_reuse":r.get("reader_reuse"),"codegen_review":r.get("codegen_review"),
            "AO_WITNESS_SETUP":True,"ordinary_owned_file_callbacks_configured":True,"consumed_validation_veto_configured":True,"callback_nesting_executed":False,"old_capture_or_matrix_replay":False,
            "native_firmware_or_device_access":False,"actual_signal_or_concurrency_execution":False,
            "native_provenance":r.get("native_provenance"),
            "fresh_witness_children":sum(len(rows) for bucket in ("candidate","controls") for rows in r.get(bucket,{}).values()),
            "attempted_supervisor_invocations":len(result.get("execution_attempts",[]))}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--docker",default=shutil.which("docker") or "/opt/homebrew/bin/docker")
    p.add_argument("--phase",choices=("build","run"),default="build")
    p.add_argument("--packet");p.add_argument("--codegen-review")
    args=p.parse_args();run=Run(args);status=1
    try:
        result=run.perform();status=0
    except BaseException as exc:
        result={"schema":1,"status":"failed","phase":run.phase,"scope":SCOPE,"runtime_gate":"CLOSED",
                "error":type(exc).__name__+": "+str(exc),"traceback":traceback.format_exc(),"inputs":run.manifest,
                "results":run.results,"commands":run.commands,"execution_attempts":run.attempts}
    finally:
        try:
            if run.owned:
                result["retained_inventory_before_result"]=inventory(run.path,EXCLUDED)
                result["retained_inventory_excludes"]=list(EXCLUDED)
                name="result-"+run.token+".json";public_name="sanitized-"+run.token+".json"
                save(run.path/name,result);projection=sanitized(result,digest(encoded(result)))
                save(run.path/public_name,projection)
                (run.path/"checkpoint.json").write_bytes(encoded({"result":name,"result_pin":metadata(encoded(result)),
                    "sanitized":public_name,"sanitized_pin":metadata(encoded(projection))}))
                print(json.dumps({"status":result["status"],"phase":run.phase,"packet":run.path.name,"result":name,
                    "private_result_sha256":digest(encoded(result)),"sanitized_result_sha256":digest(encoded(projection))},sort_keys=True))
        finally:
            if run.lock_owned:(run.path/"execution.lock").unlink()
    return status
if __name__=="__main__":sys.exit(main())
