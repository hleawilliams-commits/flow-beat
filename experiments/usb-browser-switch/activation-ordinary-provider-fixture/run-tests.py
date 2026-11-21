#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh ordinary-provider C11 witness; authored synchronous callbacks only."""
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
PARENT = HERE.parent / "activation-ordinary-entry-fixture"
PLAN_DIR = HERE.parent / "activation-ordinary-provider-plan"
PLAN_SHA = "a49b860e413b00657f0ee35b27261369ad8bade69bd0d7a2c70ea7fd1c44ab1f"
README_SHA = "292fc69adb9a5fc4464514e1981d1eaaf0be6ae785dfdfa5a5840b0f99143c24"
SCAFFOLD_SHA = "1c5b5549a43e6e4ec060541af6c91374c3793cdad562c5bb885dce6a38e726c3"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SOURCE_NAMES = ("run-tests.py", "core.c", "core.h", "source-transform.json", "test_provider.c", "test-contract.json")
METHOD_NAMES = ("ac_run", "ac_observe", "ac_admit_internal", "ac_publish_internal", "ac_receive",
                "ac_select_values", "ma_module_snapshot", "ma_generated_index", "ma_owner_index", "rc_lookup")
SETUP_NAMES = ("ac_model_stage_module", "ac_model_commit_module", "ac_model_stage_generated",
               "ac_model_commit_generated", "ac_model_add_cell")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-fno-pie"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"}
CONTROLS = {'provider-before-admission': ('AP_CONTROL_EARLY_PROVIDER', 'control-suppressed-provider', 'suppressed-entry-does-not-call-provider'), 'nested-loser-return': ('AP_CONTROL_WRONG_SUPPRESSED_RETURN', 'control-nested-return', 'nested-entry-returns-suppressed'), 'omit-unblamed-call': ('AP_CONTROL_OMIT_UNBLAMED', 'control-unblamed-dispatch', 'unblamed-callback-is-invoked-once'), 'alternate-context': ('AP_CONTROL_ALTERNATE_CONTEXT', 'control-bound-context', 'callback-receives-bound-context'), 'decoy-write-value': ('AP_CONTROL_DECOY_WRITE', 'control-write-payload', 'write-callback-receives-recorded-payload'), 'replace-close-return': ('AP_CONTROL_REPLACE_CLOSE_RETURN', 'control-close-return', 'close-receipt-matches-callback-return'), 'repeat-short-write': ('AP_CONTROL_REPEAT_SHORT_WRITE', 'control-short-write-once', 'short-write-dispatch-is-single-attempt'), 'narrow-open-token': ('AP_CONTROL_NARROW_OPEN_TOKEN', 'control-wide-token', 'accepted-open-token-reaches-followup-callbacks')}
EXPECTED_CHECK_SUM = 270
EXPECTED_CHECK_DISTINCT = 30
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault|FIXTURE_ABORT|warning:|error:", re.I)
MAX_REPORT = 1024*1024
SCOPE = "Offline C11 ordinary-provider witness with fixed authored callbacks and bounded nested losers"
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

    def reviewed_fini_warning(self, stderr, stdout):
        """Classify one exact metadata diagnostic from this object, never runtime output."""
        require(self.kind == 1, "warning classification requires relocatable object")
        rows = [(i, x) for i, x in enumerate(self.sections) if x["name"] == ".rela.fini_array.00099"]
        require(len(rows) == 1, "one named fini relocation section")
        index, rela = rows[0]
        require(rela["type"] == 4 and rela["entry_size"] == 24 and rela["size"] == 24
                and rela["flags"] == 64 and rela["alignment"] == 8
                and rela["link"] < len(self.sections) and self.sections[rela["link"]]["type"] == 2
                and rela["info"] < len(self.sections), "single RELA linked to symbol table and target")
        target = self.sections[rela["info"]]
        require(target["name"] == ".fini_array.00099" and target["type"] == 15 and target["flags"] == 3
                and target["size"] == 8 and target["alignment"] == 8 and target["entry_size"] == 8
                and target["data"] == bytes(8), "one aligned writable allocated FINI_ARRAY cell")
        offset, info, addend = struct.unpack("<QQq", rela["data"])
        symbol_index, relocation_type = info >> 32, info & 0xffffffff
        require(offset == 0 and relocation_type == 257 and symbol_index < len(self.symbols),
                "one AArch64 ABS64 relocation at array cell zero")
        symbol = self.symbols[symbol_index]
        require(symbol["type"] == 3 and symbol["value"] == 0 and symbol["size"] == 0
                and 0 < symbol["section"] < len(self.sections)
                and self.sections[symbol["section"]]["name"] == ".text",
                "relocation names the defined text section symbol")
        name = "_GLOBAL__sub_D_00099_0_rc_lookup"
        finalizer = self.function(name)
        require(finalizer["section"] == symbol["section"] and finalizer["value"] == addend
                and addend >= 0 and addend % 4 == 0 and finalizer["size"] == 32,
                "relocation exactly addresses bounded named compiler finalizer")
        expected = ("readelf: Warning: [%2d]: Info field (%d) should index a relocatable section.\n"
                    % (index, rela["info"])).encode()
        require(stderr == expected, "only exact object-derived FINI_ARRAY diagnostic")
        text = stdout.decode("utf-8", "strict")
        require(not DIAGNOSTIC.search(text) and name in text
                and "Relocation section '.rela.fini_array.00099'" in text and "FINI_ARRAY" in text,
                "retained metadata stdout contains corresponding symbol and relocation")
        keys = ("name", "type", "flags", "link", "info", "alignment", "entry_size", "size")
        return {"classification": "single FINI_ARRAY relocation to bounded named compiler finalizer",
                "section": dict(index=index, **{k: rela[k] for k in keys}),
                "target": dict(index=rela["info"], **{k: target[k] for k in keys}),
                "relocation": {"offset": offset, "type": relocation_type, "symbol_index": symbol_index,
                               "symbol_section": symbol["section"], "addend": addend},
                "finalizer": {"name": name, "extent": self.extent(name)},
                "scope": "Metadata classification only; no tool-defect cause or runtime safety inference"}

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
        planraw=regular(PLAN_DIR/"provider-plan.json")
        require(digest(planraw)==PLAN_SHA and digest(regular(PLAN_DIR/"README.md"))==README_SHA,"retained ordinary plan pins")
        plan=decode(planraw)
        for name in ("provider-plan.json","README.md","result.json","verification.json"):
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
            and set(transform["files"])=={"core.c","core.h"},"exact engine transformation scope")
        require(transform["plan"]=={"path":(PLAN_DIR/"provider-plan.json").relative_to(REPO).as_posix(),
                **metadata(planraw)},"transformation binds retained provider plan")
        require(transform["reader_inputs"]==[s for s in plan["sources"] if s["name"] in
                ("readers/readers.inc","readers/receipt-types.h","reference/reader-extraction.json")],
                "transformation binds exact retained readers")
        for name,spec in transform["files"].items():
            old=regular(self.inputs/("frozen/engine/"+name))
            new=regular(self.inputs/("new/"+name))
            require(metadata(old)==spec["old"] and metadata(new)==spec["new"],"transformed whole files")
            cursor=0; pieces=[]
            for change in spec["replacements"]:
                start,end=change["old_span"];before=change["before"].encode();after=change["after"].encode()
                require(cursor<=start<=end<=len(old) and old[start:end]==before and metadata(before)==change["before_pin"] and metadata(after)==change["after_pin"],"exact nonoverlapping transformation")
                pieces.extend((old[cursor:start],after));cursor=end
            pieces.append(old[cursor:]); require(b"".join(pieces)==new,"complete transformation reconstructs successor")
        generated=self.work/"generated"; generated.mkdir()
        for name in ("readers.inc","receipt-types.h"):
            with (generated/name).open("xb") as stream:stream.write(regular(self.inputs/("frozen/readers/"+name)))
        self.results["reader_reuse"]={"artifacts":inventory(generated)["files"],"extractor_executed":False,"scaffold":metadata(scaffold)}
        self.check_inputs()
        self.refresh_artifacts()
        self.phase="prepared"
    def check_contract(self):
        c=self.contract
        require(c["schema"]==1 and c["status"]=="source-frozen-not-executed" and c["configuration"]=="AO_WITNESS_SETUP","provider contract")
        require(c["summary_keys"]==["schema","selector","checks","check_names","failures","failed_checks"] and len(c["selectors"])==22 and set(c["controls"])==set(CONTROLS),"exact fresh selector/control scope")
        require(c["limits"]=={"candidate_selectors_per_mode":22,"control_selectors_per_mode":8,
            "checks_per_selector":20,"setup_records":16,"calls":3,"callbacks":12,"contexts":4,"event_slots":6,"report_bytes":MAX_REPORT},"closed fresh matrix limits")
        require(c["evidence_schema"]["closed_objects"] is True and c["evidence_schema"]["summary_keys"]==c["summary_keys"],"closed report schema")
        require(type(EXPECTED_CHECK_SUM) is int and type(EXPECTED_CHECK_DISTINCT) is int,"prebuild assertion counts frozen")
        require(sum(len(row["checks"]) for row in c["selectors"].values())==EXPECTED_CHECK_SUM
            and len({n for row in c["selectors"].values() for n in row["checks"]})==EXPECTED_CHECK_DISTINCT,"fresh assertion counts")
        require(c["summary_counts"]=={"normal_selectors":14,"control_baselines":8,
            "candidate_selectors_per_mode":22,"controls_per_mode":8,
            "summed_assertions_per_mode":EXPECTED_CHECK_SUM,
            "distinct_assertion_names":EXPECTED_CHECK_DISTINCT,"fresh_runtime_processes":60},
            "exact predeclared process and assertion summary")
        plan=decode(regular(self.inputs/"plan/provider-plan.json"))
        normal=[n for g in plan["cases"] for n in g["selectors"]]
        require(set(c["selectors"])==set(normal)|{x["selector"] for x in plan["safe_controls"]} and len(normal)==14,"exact planned selectors")
        for name,row in c["selectors"].items():
            relative(name);checks=row["checks"]
            require(0<len(checks)<=20 and len(checks)==len(set(checks)) and all(re.fullmatch(r"[a-z][a-z0-9-]+",x) for x in checks),"named exact checks")
        for name,(macro,selector,failure) in CONTROLS.items():
            v=c["controls"][name]
            require(v["macro"]==macro and v["selector"]==selector and v["expected_failures"]==[failure] and failure in c["selectors"][selector]["checks"],"exact safe control failure set")
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
        container="az-provider-"+self.path.name[4:]+"-"+str(len(self.commands)+1)
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
        require(type(image) is list and len(image)==1 and image[0]["Id"]==IMAGE_ID and image[0]["Architecture"]=="arm64" and image[0]["Os"]=="linux","pinned offline image")
        version=self.clean("compiler-version",["gcc","--version"]);target=self.clean("compiler-target",["gcc","-dumpmachine"])
        require(b"7.5.0" in version.splitlines()[0] and target.strip()==b"aarch64-linux-gnu","GCC 7.5 AArch64")
        self.results["image"]={k:image[0][k] for k in ("Id","Architecture","Os")}
        self.results["toolchain"]={"version":metadata(version),"target":target.decode().strip()}
    def review_object(self,key,folder,setup,control):
        raw=regular(folder/"core.o");elf=Elf(raw,1)
        present={s["name"] for s in elf.symbols if s["section"]}
        require(not any(n.startswith("ac_fixture_") or n in ("ac_model_diagnostic","ac_aux","ac_snapshot_refused") for n in present),"private fixture symbols absent")
        require({n for n in present if n.startswith("ac_model_")}==(set(SETUP_NAMES) if setup else set()),"exact setup-only symbol surface")
        for n in ("ac_run","ac_observe"):elf.function(n)
        if control is None:
            for n in METHOD_NAMES:elf.function(n)
        target="/out/"+folder.relative_to(self.work).as_posix()+"/core.o"
        dis=self.clean(key+"-core-disassembly",["objdump","-dr",target])
        rc,out,err=self.docker(key+"-core-elf",["readelf","-W","-h","-S","-s","-r",target])
        require(rc==0 and not DIAGNOSTIC.search(out.decode("utf-8","replace")),"metadata command stdout")
        warning=elf.reviewed_fini_warning(err,out) if err else None
        require(regular(folder/"core.o")==raw,"metadata preserves object")
        (folder/"core-disassembly.txt").write_bytes(dis);(folder/"core-elf.txt").write_bytes(out)
        save(folder/"core-metadata-review.json",{"schema":1,"status":"reviewed-fini-array-warning" if warning else "clean-metadata",
             "object":metadata(raw),"stdout":metadata(out),"stderr":metadata(err),"warning":warning,"returncode":rc,"command":key+"-core-elf"})
        names=METHOD_NAMES+(SETUP_NAMES if setup else ())
        save(folder/"core-methods.json",{n:elf.extent(n) for n in names if n in present})
        save(folder/"core-symbols.json",elf.summary())
        save(folder/"core-undefined.json",{"undefined_symbols":[s for s in elf.symbols if not s["section"] and s["name"]],"scope":"Direct dependencies only; no transitive/signal proof"})
        return elf
    def preflight(self):
        require(not self.args.packet and self.args.phase=="build","first owned preflight only")
        self.inspect_toolchain()
        for mode in ("plain","asan-ubsan"):
            for variant in ("standalone","setup"):
                key=variant+"-"+mode;folder=self.work/"build"/variant/mode;folder.mkdir(parents=True)
                flags=self.flags(mode,variant=="setup");target="/out/"+folder.relative_to(self.work).as_posix()
                self.clean("compile-"+key+"-core",["gcc",*flags,"-I/src/new","-I/out/generated","-c","/src/new/core.c","-o",target+"/core.o"],empty=True)
                self.review_object(key,folder,variant=="setup",None)
                save(folder/"configuration.json",{"flags":flags,"AO_WITNESS_SETUP":variant=="setup","compile_only":True,"control":None})
                self.results["builds"][key]={"variant":variant,"mode":mode,"kind":"preflight","compile_only":True,"macro":None,
                    "artifacts":{p.relative_to(self.work).as_posix():metadata(regular(p)) for p in sorted(folder.iterdir())}}
        self.refresh_artifacts();self.check_inputs();self.phase="built-preflight"
    def adopt_review(self):
        self.check_inputs();self.check_artifacts()
        raw=regular(Path(self.args.codegen_review));v=decode(raw)
        expected={k:b["artifacts"] for k,b in self.results["builds"].items()}
        require(set(v)=={"schema","status","runtime_gate","review_lock","generated","preflight","method_reviews","guard_failure_review","limitations"} and
                v["schema"]==1 and v["status"]=="reviewed-before-candidate-runtime" and v["runtime_gate"]=="CLOSED","fresh reviewed preflight schema")
        require(v["review_lock"]==metadata(regular(self.inputs/"review-lock.json")) and v["generated"]==self.results["reader_reuse"]["artifacts"] and v["preflight"]==expected,"review binds all four fresh object packets")
        require(set(v["method_reviews"])==set(expected),"review every configuration")
        for key,rows in v["method_reviews"].items():
            variant,mode=key.split("-",1)
            methods=decode(regular(self.work/"build"/variant/mode/"core-methods.json"))
            require(set(rows)==set(methods),"all baseline and setup methods reviewed")
            for name,row in rows.items():
                require(row["extent"]==methods[name] and type(row["observations"]) is list and row["observations"] and all(type(s) is str and s for s in row["observations"]),"method-bound source/codegen review")
        require(type(v["guard_failure_review"]) is str and v["guard_failure_review"] and type(v["limitations"]) is list and v["limitations"],"dependency and control-flow limits")
        folder=self.work/"review";folder.mkdir()
        with (folder/"codegen-review.json").open("xb") as stream:stream.write(raw)
        self.results["codegen_review"]={"artifact":metadata(raw),"runtime_gate":"CLOSED"}
        self.refresh_artifacts()
    def executable(self,variant,mode):
        key=variant+"-"+mode;require(key not in self.results["builds"],"no successful build replay")
        folder=self.work/"build"/variant/mode;folder.mkdir(parents=True)
        target="/out/"+folder.relative_to(self.work).as_posix()
        control=None if variant=="candidate" else variant;flags=self.flags(mode,True,control)
        reuse={}
        if control is None:
            source=self.work/"build/setup"/mode/"core.o";raw=regular(source)
            (folder/"core.o").write_bytes(raw);reuse["core.o"]={"source":source.relative_to(self.work).as_posix(),**metadata(raw)}
            self.clean("compile-"+key+"-harness",["gcc",*self.flags(mode,True),"-I/src/new","-I/out/generated","-c","/src/new/test_provider.c","-o",target+"/test_provider.o"],empty=True)
        else:
            self.clean("compile-"+key+"-core",["gcc",*flags,"-I/src/new","-I/out/generated","-c","/src/new/core.c","-o",target+"/core.o"],empty=True)
            self.review_object(key,folder,True,control)
            source=self.work/"build/candidate"/mode/"test_provider.o";raw=regular(source)
            (folder/"test_provider.o").write_bytes(raw);reuse["test_provider.o"]={"source":source.relative_to(self.work).as_posix(),**metadata(raw)}
        self.clean("link-"+key,["gcc",*flags,"-no-pie",target+"/core.o",target+"/test_provider.o","-o",target+"/test-provider"],empty=True)
        elf=Elf(regular(folder/"test-provider"),2);elf.function("ac_run");elf.function("ac_observe")
        save(folder/"linked-symbols.json",elf.summary())
        (folder/"linked-disassembly.txt").write_bytes(self.clean(key+"-linked-disassembly",["objdump","-d",target+"/test-provider"]))
        save(folder/"configuration.json",{"flags":flags,"AO_WITNESS_SETUP":True,"compile_only":False,"control":control,"same_packet_artifact_reuse":reuse})
        self.results["builds"][key]={"variant":variant,"mode":mode,"kind":"witness","compile_only":False,"macro":None if control is None else CONTROLS[control][0],
            "artifacts":{p.relative_to(self.work).as_posix():metadata(regular(p)) for p in sorted(folder.iterdir())},"binary":target+"/test-provider","reuse":reuse}
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
        require(set(p.name for p in evidence.iterdir())=={"report.json"},"one bounded owned report")
        raw=regular(evidence/"report.json");require(0<len(raw)<=MAX_REPORT,"bounded report")
        report=decode(raw);require({k:report[k] for k in self.contract["summary_keys"]}==summary,"public/private report agreement")
        self.report_shape(report)
        bucket=self.results["candidate"] if variant=="candidate" else self.results["controls"]
        bucket.setdefault(mode,{})[selector if variant=="candidate" else variant]={"selector":selector,"summary":summary,"report":metadata(raw),
            "command":name,"returncode":rc,"stdout":metadata(out),"stderr":metadata(err),"report_path":(evidence/"report.json").relative_to(self.work).as_posix()}
        self.refresh_artifacts()
    def run(self):
        require(self.args.packet and self.args.codegen_review,"reviewed preflight required")
        self.adopt_review()
        for mode in ("plain","asan-ubsan"):
            for variant in ("candidate",*CONTROLS):self.executable(variant,mode)
            for selector in self.contract["selectors"]:self.run_case("candidate",mode,selector)
            for variant,(_,selector,_) in CONTROLS.items():self.run_case(variant,mode,selector)
        self.check_inputs();self.check_artifacts();self.phase="completed-new-provider-matrix"
    def report_shape(self,report):
        # Structural only: intentional singleton controls may return wrong enums
        # or omit completion; their semantic oracle belongs to the fixed harness.
        schema=self.contract["evidence_schema"]
        require(type(report) is dict and set(report)==set(schema["private_keys"]),"closed private report")
        require(report["source_scope"]==schema["source_scope"],"declared authored provider scope")
        def value(item,kind):
            if kind in schema["objects"]:
                fields=schema["objects"][kind]
                require(type(item) is dict and set(item)==set(fields),"closed value object: "+kind)
                for name,sub in fields.items():value(item[name],sub)
                return
            if kind=="bool":require(type(item) is bool,"boolean report value");return
            if kind=="hex33":require(type(item) is str and re.fullmatch(r"[0-9a-f]{66}",item) is not None,"full33-byte report value");return
            limits={"int64":(-(1<<63),(1<<63)-1),"int32":(-(1<<31),(1<<31)-1),
                    "uint64":(0,(1<<64)-1),"uint32":(0,(1<<32)-1),"uint[0,6]":(0,6)}
            if kind in limits:
                low,high=limits[kind];require(type(item) is int and low<=item<=high,"bounded integer: "+kind);return
            if kind.startswith("enum[") and kind.endswith("]"):
                choices=kind[5:-1].split(",")
                if all(x.isdigit() for x in choices):
                    require(type(item) is int and item in [int(x) for x in choices],"bounded numeric enum")
                else:require(type(item) is str and item in choices,"bounded string enum")
                return
            arrays={"event[6]":("event",6,6),"call_record[1..3]":("call_record",1,3),
                    "setup_record[0..16]":("setup_record",0,16),"callback_record[0..12]":("callback_record",0,12),
                    "context[4]":("context",4,4),"uint32[6]":("uint32",6,6),
                    "uint32[0..12]":("uint32",0,12)}
            require(kind in arrays,"known structural type")
            member,low,high=arrays[kind];require(type(item) is list and low<=len(item)<=high,"bounded report array")
            for child in item:value(child,member)
        for name,kind in schema["private_fields"].items():value(report[name],kind)
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
            "AO_WITNESS_SETUP":True,"providers_are_authored_callbacks":True,"bounded_same_thread_nested_loser_scope":True,"old_capture_or_matrix_replay":False,
            "native_firmware_or_device_access":False,"actual_signal_or_concurrency_execution":False}
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
