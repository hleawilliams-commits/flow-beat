#!/usr/bin/env python3
"""Independent retained-byte and exact-oracle audit; imports/executes no runner."""
import json, hashlib, re, stat, sys, uuid
from pathlib import Path

EXP=Path(__file__).resolve().parent
HERE=EXP/"private"/("audit-"+uuid.uuid4().hex)
HERE.mkdir(parents=True,exist_ok=False)
INPUTS={"reporter.rs","tests.rs","parent-inputs.json","candidate-pins.json",
        "prepare-candidate.py","run-tests.py","controls.json","test-inventory.json"}
METADATA={".git/HEAD":b"ref: refs/heads/master\n",
          ".git/config":b"[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n\tlogallrefupdates = true\n\tignorecase = true\n\tprecomposeunicode = true\n"}
PREFIX="reader_reporter::tests::"
def need(ok,msg):
    if not ok: raise RuntimeError(msg)
def sha(b): return hashlib.sha256(b).hexdigest()
def raw(p):
    need(stat.S_ISREG(p.lstat().st_mode),"nonregular file")
    return p.read_bytes()
def js(p): return json.loads(raw(p))
def files(p):
    need(p.is_dir() and not p.is_symlink(),"nonreal directory")
    out={}
    for x in sorted(p.rglob("*")):
        mode=x.lstat().st_mode
        need(stat.S_ISDIR(mode) or stat.S_ISREG(mode),"special retained node")
        if stat.S_ISREG(mode): out[x.relative_to(p).as_posix()]=raw(x)
    return out
def hashes(v): return {k:sha(b) for k,b in v.items()}
def edit(b,old,new):
    old,new=old.encode(),new.encode()
    need(old!=new and b.count(old)==1,"nonunique edit")
    return b.replace(old,new,1)
def panic_records_match(text,wanted):
    found=re.findall(r"^thread '([^'\n]+)'(?: \([0-9]+\))? panicked at ([^\n]+):$",text,re.M)
    return text.count(" panicked at ")==len(found) and found==wanted
def check(root,i):
    need(root.parent==EXP/"private" and re.fullmatch(r"prepare-[0-9a-f]{32}",root.name),"foreign matrix")
    r=js(root/"result.json");p=js(root/"reporter-preparation.json")
    need(r["status"]=="passed" and p["status"]=="verified","failed/incomplete matrix")
    need(r["host_only"]==bool(i) and r["production_check_exit"]==(None if i else 0),"production scope")
    need((r["positive_count"],r["strict_control_count"],r["inherited_tests_filtered"])==(8,4,267),"counts")
    inputs=files(root/"inputs")
    need(set(inputs)==INPUTS and hashes(inputs)==r["inputs_sha256"],"capture inventory/hash")
    need(all(raw(EXP/n)==b for n,b in inputs.items()),"input drift")
    need(p["inputs_sha256"]==hashes({n:b for n,b in inputs.items() if n not in {"run-tests.py","controls.json","test-inventory.json"}}),"prep capture")
    parents=files(root/"reporter-parent-inputs");pp=json.loads(inputs["parent-inputs.json"])
    need(len(parents)==48 and hashes(parents)==pp==p["parent_inputs_sha256"],"48 frozen captures")
    need(all(raw(EXP.parent/n)==b for n,b in parents.items()),"parent drift")
    original=files(root/"reporter-parent-tree")
    need(len(original)==32 and hashes(original)==json.loads(parents["emulator-supervisor-admission/candidate-pins.json"])==p["parent_files_sha256"],"parent32")
    candidate=dict(original)
    main="az-preview/src/main.rs"
    anchor="#[cfg(unix)]\nmod supervisor_admission;"
    candidate[main]=edit(candidate[main],anchor,anchor+"\n#[cfg(unix)]\nmod reader_reporter;")
    candidate["az-preview/src/reader_reporter.rs"]=inputs["reporter.rs"]
    candidate["az-preview/src/reader_reporter_tests.rs"]=inputs["tests.rs"]
    pins=json.loads(inputs["candidate-pins.json"])
    need(len(candidate)==34 and hashes(candidate)==pins==p["candidate_files_sha256"]==r["candidate_files_sha256"],"candidate34")
    need(p["changed_parent_files"]==[main],"main-only declaration")
    need(files(root/"tree")==dict(candidate,**METADATA),"source and exact Git metadata")
    inv=json.loads(inputs["test-inventory.json"]);selected=inv["selected"];alltests=inv["all"]
    need(len(selected)==8 and selected==sorted(set(selected)) and all(n.startswith(PREFIX) for n in selected),"selected inventory")
    need(len(alltests)==275 and alltests==sorted(set(alltests)) and [n for n in alltests if n.startswith(PREFIX)]==selected,"compiled inventory")
    parent_inv_raw=raw(EXP.parent/"emulator-supervisor-admission/test-inventory.json")
    need(sha(parent_inv_raw)=="5f7bd179ebb487fdc6f1c03aee6d7c2711e761ee55a2c955445565b6ec3afb5a","parent inventory source pin")
    parent_inv=json.loads(parent_inv_raw)
    need(sorted(parent_inv["all"]+selected)==alltests,"267 inherited names unchanged")
    controls=json.loads(inputs["controls.json"])
    need(len(controls["controls"])==4 and all(pins[k]==v for k,v in controls["candidate_sha256"].items()),"control source binding")
    need(len(r["runs"])==5,"variant count")
    expected={"cargo.log","rustc.log"}
    if i==0:
        expected.add("production-check.log")
        txt=raw(root/"production-check.log").decode()
        need("Finished" in txt and "az-preview" in txt and "error[E" not in txt,"production check")
    else: need(not (root/"production-check.log").exists(),"repeat scope")
    for name in ("cargo","rustc"): need(raw(root/(name+".log")).decode().strip()==r["versions"][name],"compiler version")
    outcomes=[]
    for row,control in zip(r["runs"],[None]+controls["controls"]):
        name=control["name"] if control else "candidate"
        need(row["name"]==name and row["compile_exit"]==0,"variant identity")
        variant=dict(candidate)
        if control:
            need(control["test"] in selected,"inherited control selected")
            for e in control.get("edits",[control]):
                need(e["file"]=="az-preview/src/reader_reporter.rs","non-reporter control")
                variant[e["file"]]=edit(variant[e["file"]],e["old"],e["new"])
        folder=root/name
        need(files(folder/"tree")==variant and hashes(variant)==row["source_sha256"],"variant bytes")
        need(sha(raw(folder/"tests-bin"))==row["executable_sha256"],"retained executable")
        build=[json.loads(s) for s in raw(folder/"compile.log").decode().splitlines() if s.startswith("{")]
        finished=[m for m in build if m.get("reason")=="build-finished"]
        need(len(finished)==1 and finished[0]["success"] is True,"compile completion")
        need(not any(m.get("reason")=="compiler-message" and m.get("message",{}).get("level")=="error" for m in build),"compile error")
        artifacts=[m for m in build if m.get("reason")=="compiler-artifact" and m.get("executable") and m.get("profile",{}).get("test")]
        need(len(artifacts)==1 and Path(artifacts[0]["executable"]).is_relative_to(root/"target"),"compiled executable origin")
        listed=raw(folder/"inventory.log").decode()
        need(sorted(s[:-6] for s in listed.splitlines() if s.endswith(": test"))==alltests,"binary inventory")
        expected.update({name+"/compile.log",name+"/inventory.log"})
        cases=[control["test"]] if control else selected
        need(len(row["tests"])==len(cases),"selected row count")
        for j,(t,test) in enumerate(zip(row["tests"],cases)):
            logname=name+f"/case-{j}.log";expected.add(logname)
            log=raw(root/logname);text=log.decode()
            need(t["test"]==test and t["log_sha256"]==sha(log),"case provenance")
            need(text.count("running 1 test")==1 and "FIXTURE_ABORT" not in text,"fixture/selector failure")
            need(not re.search(r"fatal runtime error|stack overflow|AddressSanitizer|ThreadSanitizer|runtime error:",text),"runtime diagnostic")
            if control:
                lines=variant["az-preview/src/reader_reporter_tests.rs"].decode().splitlines()
                starts=[n for n,line in enumerate(lines) if line.startswith("fn "+test.rsplit("::",1)[-1]+"(")]
                need(len(starts)==1,"named oracle function")
                start=starts[0];end=next((n for n in range(start+1,len(lines)) if lines[n]=="#[test]"),len(lines))
                hits=[n for n in range(start,end) if control["assertion_anchor"] in lines[n]]
                need(len(hits)==1 and "\n".join(lines[start:end]).count(control["assertion_message"])==1,"oracle anchor")
                n=hits[0];loc=f"reader_reporter_tests.rs:{n+1}:{lines[n].index(control['assertion_anchor'])+1}:"
                need(t["exit"]==101 and t["expected_assertion_location"]==loc and loc in text and control["assertion_message"] in text,"exact assertion outcome")
                need("test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 274 filtered out;" in text and test+" ... FAILED" in text,"negative summary")
            else:
                loc=None
                need(t["exit"]==0 and "test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 274 filtered out;" in text and test+" ... ok" in text,"positive summary")
            if control:
                wanted_panics=[(test,"src/"+loc.removesuffix(":"))]
            elif test==PREFIX+"reader_panic_is_joined_without_replay":
                source=variant["az-preview/src/reader_reporter_tests.rs"].decode()
                marker='std::panic::panic_any(PanicFault { marker: "exact-reader-panic" })'
                need(source.count(marker)==1,"intentional worker panic site")
                pos=source.index(marker)
                site="src/reader_reporter_tests.rs:"+str(source.count("\n",0,pos)+1)+":"+str(pos-source.rfind("\n",0,pos))
                wanted_panics=[("terminal-reader",site)]
            else:
                wanted_panics=[]
            need(panic_records_match(text,wanted_panics),"unexpected/missing/unparsed panic record")
            outcomes.append({"variant":name,"test":test,"exit":t["exit"],"oracle":loc})
    logs={x.relative_to(root).as_posix():sha(raw(x)) for x in root.rglob("*.log") if "target" not in x.relative_to(root).parts}
    need(set(logs)==expected and logs==r["logs_sha256"] and len(logs)==(24 if i else 25),"complete log inventory")
    return r,{"label":f"matrix_{i+1}","result_sha256":sha(raw(root/"result.json")),
              "preparation_sha256":sha(raw(root/"reporter-preparation.json")),"checked_logs":len(logs),
              "captured_inputs":8,"frozen_parent_files":48,"original_files":32,"composed_files":34,
              "retained_executables":5,"outcomes":outcomes,"metadata_sha256":hashes(METADATA)}
def project(r):
    r=json.loads(json.dumps(r));excluded=["/host_only","/production_check_exit"]
    r.pop("host_only");r.pop("production_check_exit")
    excluded.extend("/logs_sha256/"+k for k in sorted(r.pop("logs_sha256")))
    for i,row in enumerate(r["runs"]):
        row.pop("executable_sha256");excluded.append(f"/runs/{i}/executable_sha256")
        for j,t in enumerate(row["tests"]):
            t.pop("log_sha256");excluded.append(f"/runs/{i}/tests/{j}/log_sha256")
    return r,excluded
out=HERE/"result.json";need(not out.exists(),"preserve prior audit")
result={"status":"started","audit_source_sha256":sha(raw(Path(__file__))),"runs":[]}
out.write_text(json.dumps(result,indent=2)+"\n")
try:
    # The four offline parser controls do not execute or edit a test candidate.
    sample="thread 'case' (19) panicked at src/test.rs:3:5:\nexpected assertion\n"
    wanted=[("case","src/test.rs:3:5")]
    need(panic_records_match(sample,wanted),"panic parser positive")
    bad=[sample+sample, sample+"thread \"fixture\" panicked at unknown\n",
         "no panic record\n", sample.replace("src/test.rs:3:5","src/test.rs:9:5")]
    need(all(not panic_records_match(text,wanted) for text in bad),"extra/missing/wrong panic controls")
    roots=[Path(s).resolve() for s in sys.argv[1:]]
    need(len(roots)==2 and roots[0]!=roots[1],"two fresh matrices required")
    rs=[]
    for i,root in enumerate(roots):
        r,checked=check(root,i);rs.append(r);result["runs"].append(checked)
    a,ea=project(rs[0]);b,eb=project(rs[1]);need(a==b,"semantic projection differs")
    common=sorted(set(rs[0]["logs_sha256"])&set(rs[1]["logs_sha256"]))
    extra=[]
    attempts=sorted(x for x in (EXP/"private").iterdir() if x.is_dir() and re.fullmatch(r"prepare-[0-9a-f]{32}",x.name))
    for p in attempts:
        if p in roots: continue
        path=p/"result.json" if (p/"result.json").exists() else p/"reporter-preparation.json"
        r=js(path);need(r["status"]=="failed","unselected nonfailed attempt")
        extra.append({"name":p.name,"receipt_sha256":sha(raw(path)),"failure":r.get("failure")})
    result.update(status="passed",successful_matrices=2,failed_extra_attempts=extra,
                  all_top_level_prepare_attempts=len(attempts),checked_logs=49,retained_executables=10,
                  selected_executions=24,production_checks=1,inherited_selectors_filtered=267,
                  panic_policy_negative_controls=4, exact_panic_inventory_verified=True,
                  semantic_projection_equal=True,exact_exclusions={"matrix_1":ea,"matrix_2":eb},
                  different_raw_logs=[k for k in common if rs[0]["logs_sha256"][k]!=rs[1]["logs_sha256"][k]],
                  common_logs=len(common),raw_or_normalized_log_equality_claim=False,
                  scope="Independent retained-byte/source/oracle corroboration; no runner import, execution or durable/adversarial attestation.")
except BaseException as e:
    result.update(status="failed",failure=type(e).__name__+": "+str(e))
    out.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n");raise
out.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
print("Private audit:",HERE)
print(json.dumps({"status":result["status"],"audit_sha256":result["audit_source_sha256"],"result_sha256":sha(raw(out))}))
