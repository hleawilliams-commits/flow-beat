// SPDX-License-Identifier: GPL-3.0-only
use super::*;
use crate::mixer_producer::Producer;
use std::process::{ChildStdin, ChildStdout, Command, Stdio};
use std::sync::mpsc;
use std::thread;
use std::time::Duration;
const WAIT: Duration = Duration::from_secs(3);

fn config() -> Config {
    Config { initial: State { instance: [0x0102030405060708,0x1112131415161718],
        run_id: 0x2122232425262728, epoch: 0x3132333435363738, sequence: 7,
        flags: 31, crc_len: 96, frame: std::array::from_fn(|i| (i*3+7) as u8) },
        producer: 0x4142434445464748, session: std::array::from_fn(|i| 0x51+i as u8) }
}
fn baseline(c: Config) -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: c.initial.instance, run_id: c.initial.run_id,
        epoch: c.initial.epoch, producer_id: c.producer },
        frame: c.initial.frame, kernel_sequence: c.initial.sequence }
}
fn requests(c: Config) -> (Envelope, Envelope) {
    let p = Producer::new(4).unwrap(); let b = baseline(c);
    p.stage_baseline(b).unwrap(); p.acknowledge_baseline(b).unwrap();
    p.confirm_released(b, true).unwrap();
    (p.create_for_key(b.key, std::array::from_fn(|i| (i*5+11) as u8)).unwrap(),
     p.create_for_key(b.key, std::array::from_fn(|i| (i*7+13) as u8)).unwrap())
}
// Independent append-only canonical oracle: no production encoder/decoder calls.
fn word(b: &mut Vec<u8>, n: u32) { b.extend_from_slice(&n.to_be_bytes()); }
fn wide(b: &mut Vec<u8>, n: u64) { b.extend_from_slice(&n.to_be_bytes()); }
fn head(kind: u32, length: u32, c: Config, id: u64) -> Vec<u8> {
    let mut b = b"AZMBRPC1".to_vec();
    for n in [1,kind,length,0] { word(&mut b,n); }
    b.extend_from_slice(&c.session); wide(&mut b,id); b
}
fn state_bytes(s: State) -> Vec<u8> {
    let mut b=vec![];
    for n in [s.instance[0],s.instance[1],s.run_id,s.epoch,s.sequence] { wide(&mut b,n); }
    word(&mut b,s.flags); b.extend_from_slice(&s.crc_len.to_be_bytes());
    b.extend_from_slice(&s.frame); b
}
fn grant(c: Config) -> Vec<u8> {
    let mut b=vec![];
    for n in [c.initial.instance[0],c.initial.instance[1],c.initial.run_id,
        c.initial.epoch,c.producer] { wide(&mut b,n); } b
}
fn request_bytes(e: Envelope) -> Vec<u8> {
    let k=e.key(); let mut b=vec![];
    for n in [k.instance[0],k.instance[1],k.run_id,k.epoch,k.producer_id,e.request_id()] {
        wide(&mut b,n);
    }
    b.extend_from_slice(e.frame()); b
}
fn adopt_request(c: Config) -> Vec<u8> {
    let mut b=head(1,256,c,1); b.extend(state_bytes(c.initial)); wide(&mut b,c.producer); b
}
fn frame_request(c: Config, e: Envelope, id: u64) -> Vec<u8> {
    let mut b=head(2,248,c,id); b.extend(request_bytes(e)); b
}
fn adopt_reply(c: Config) -> Vec<u8> {
    let mut b=head(0x80000001,296,c,1); word(&mut b,0); word(&mut b,0);
    b.extend(state_bytes(c.initial)); b.extend(grant(c)); b
}
fn frame_reply(c: Config, e: Envelope, id: u64, seq: u64, outcome: u32, error: i32) -> Vec<u8> {
    let mut b=head(0x80000002,668,c,id);
    for n in [0,0,outcome,1,error as u32,0] { word(&mut b,n); }
    b.extend(request_bytes(e));
    let k=e.key();
    for n in [k.instance[0],k.instance[1],k.run_id,k.epoch,seq] { wide(&mut b,n); }
    word(&mut b,1); b.extend_from_slice(e.frame());
    let mut after=c.initial; after.sequence=seq; after.frame=*e.frame();
    b.extend(state_bytes(after)); b
}
fn replies(c: Config, e: Envelope) -> Vec<u8> {
    [adopt_reply(c),frame_reply(c,e,2,c.initial.sequence+1,1,0)].concat()
}
fn set32(bytes: &mut [u8], at: usize, n: u32) { bytes[at..at+4].copy_from_slice(&n.to_be_bytes()); }

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct Counts { written: Vec<u8>, read: usize, calls: [usize;3], drops: usize }
type Shared = Arc<Mutex<Counts>>;
#[derive(Clone, Copy)] enum How { Zero, Interrupted, Error, OsError, Panic }
#[derive(Clone, Copy)] struct Stop { stage: IoStage, after: usize, how: How }
struct Script {
    input: Vec<u8>, pos: usize, shared: Shared, chunk: usize, stop: Option<Stop>,
}
impl Script {
    fn new(input: Vec<u8>) -> (Self, Shared) {
        let shared=Arc::new(Mutex::new(Counts::default()));
        (Self {input,pos:0,shared:shared.clone(),chunk:usize::MAX,stop:None},shared)
    }
    fn stop(&self, stage: IoStage, progress: usize) -> io::Result<Option<usize>> {
        if let Some(s)=self.stop {
            if s.stage==stage && progress>=s.after {
                return match s.how {
                    How::Zero=>Ok(Some(0)),
                    How::Interrupted=>Err(io::Error::new(io::ErrorKind::Interrupted,"injected interruption")),
                    How::Error=>Err(io::Error::new(io::ErrorKind::Other,"injected stream error")),
                    How::OsError=>Err(io::Error::from_raw_os_error(5)),
                    How::Panic=>panic!("injected stream panic"),
                };
            }
        }
        Ok(None)
    }
    fn size(&self, stage: IoStage, progress: usize, available: usize) -> usize {
        let mut n=available.min(self.chunk);
        if let Some(s)=self.stop { if s.stage==stage && s.after>progress {
            n=n.min(s.after-progress);
        }} n
    }
}
impl Write for Script {
    fn write(&mut self, b: &[u8]) -> io::Result<usize> {
        let progress={let mut s=self.shared.lock().unwrap();s.calls[0]+=1;s.written.len()};
        if let Some(n)=self.stop(IoStage::Write,progress)? {return Ok(n);}
        let n=self.size(IoStage::Write,progress,b.len());
        self.shared.lock().unwrap().written.extend_from_slice(&b[..n]); Ok(n)
    }
    fn flush(&mut self) -> io::Result<()> {
        let progress={let mut s=self.shared.lock().unwrap();let n=s.calls[1];s.calls[1]+=1;n};
        let _=self.stop(IoStage::Flush,progress)?; Ok(())
    }
}
impl Read for Script {
    fn read(&mut self,b: &mut [u8]) -> io::Result<usize> {
        {self.shared.lock().unwrap().calls[2]+=1;}
        if let Some(n)=self.stop(IoStage::Read,self.pos)? {return Ok(n);}
        let n=self.size(IoStage::Read,self.pos,b.len().min(self.input.len()-self.pos));
        b[..n].copy_from_slice(&self.input[self.pos..self.pos+n]);self.pos+=n;
        self.shared.lock().unwrap().read+=n; Ok(n)
    }
}
impl Drop for Script { fn drop(&mut self) {self.shared.lock().unwrap().drops+=1;} }
fn counts(s: &Shared) -> Counts {s.lock().unwrap().clone()}
fn backend(input: Vec<u8>, c: Config) -> (WireBackend,EvidenceHandle,Shared) {
    let (script,shared)=Script::new(input);
    let (wire,evidence)=WireBackend::retain(Box::new(script),c);(wire,evidence,shared)
}
fn committed(e: Envelope, seq: u64) -> BackendOutcome {
    BackendOutcome::Committed(FrameAck {request:e,kernel_sequence:seq})
}

#[test]
fn invalid_constructor_retains_exact_stream_without_io_or_silent_drop() {
    let c=config();
    let mut configs=vec![];
    for n in 0..8 {
        let mut bad=c;
        match n {0=>bad.initial.instance=[0,0],1=>bad.initial.run_id=0,
            2=>bad.initial.epoch=0,3=>bad.producer=0,4=>bad.session=[0;16],
            5=>bad.initial.flags=23,6=>bad.initial.crc_len=-1,
            _=>bad.initial.sequence=u64::MAX}
        configs.push(bad);
    }
    let (e,_)=requests(c);
    for bad in configs {
        let (mut wire,evidence,shared)=backend(vec![],bad);
        assert_eq!(counts(&shared),Counts::default());
        assert_eq!(evidence.snapshot().config,bad);
        assert_eq!(wire.baseline().is_err(),true);
        assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        assert_eq!(counts(&shared),Counts::default());
        assert_eq!(evidence.snapshot().terminal,Some(Fault::Config));
        drop(wire);
        assert_eq!(counts(&shared).drops,1);
        assert_eq!(evidence.snapshot().phase,Phase::Terminal);
    }
}

#[test]
fn canonical_bytes_match_independent_full_width_oracles_in_both_directions() {
    let c=config();let (e,_)=requests(c);
    let expected_adopt=adopt_request(c);let expected_frame=frame_request(c,e,2);
    assert_eq!(&expected_adopt[..24],b"AZMBRPC1\0\0\0\x01\0\0\0\x01\0\0\x01\0\0\0\0\0");
    assert_eq!(&expected_adopt[48..64],&[1,2,3,4,5,6,7,8,17,18,19,20,21,22,23,24]);
    assert_eq!(&expected_adopt[248..256],&[65,66,67,68,69,70,71,72]);
    let (mut wire,evidence,shared)=backend(replies(c,e),c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    assert_eq!(evidence.snapshot().pending.unwrap().as_bytes(),expected_adopt);
    assert_eq!(wire.submit(e),committed(e,8));
    let snapshot=evidence.snapshot();
    assert_eq!(snapshot.pending.unwrap().as_bytes(),expected_frame);
    assert_eq!(snapshot.response.as_bytes(),frame_reply(c,e,2,8,1,0));
    assert_eq!(counts(&shared).written,[expected_adopt,expected_frame].concat());
    assert_eq!(snapshot.expected.frame,*e.frame());
    assert_eq!(snapshot.expected.sequence,8);
    assert_eq!(snapshot.next_exchange,3);
    assert_eq!(snapshot.next_request,2);
}

#[test]
fn positive_short_io_continues_offsets_and_flushes_exactly_once_per_record() {
    let c=config();let (e,_)=requests(c);
    let (mut script,shared)=Script::new(replies(c,e));script.chunk=7;
    let (mut wire,evidence)=WireBackend::retain(Box::new(script),c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    assert_eq!(wire.submit(e),committed(e,8));
    let s=counts(&shared);let ev=evidence.snapshot();
    assert_eq!(s.written,[adopt_request(c),frame_request(c,e,2)].concat());
    assert_eq!(s.read,296+668);
    assert_eq!(s.calls[1],2);
    assert_eq!(ev.written,248);
    assert_eq!(ev.read,668);
    assert_eq!(ev.flush_started,true);
    assert_eq!(ev.flush_completed,true);
}

#[test]
fn baseline_is_one_shot_and_terminal_reuse_never_adopts_or_writes_again() {
    let c=config();let (e,_)=requests(c);
    let (mut wire,evidence,shared)=backend(replies(c,e),c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    let before=counts(&shared);
    assert_eq!(wire.baseline().is_err(),true);
    assert_eq!(wire.submit(e),BackendOutcome::Unknown);
    assert_eq!(wire.submit(e),BackendOutcome::Unknown);
    assert_eq!(counts(&shared),before);
    assert_eq!(evidence.snapshot().pending.unwrap().as_bytes(),adopt_request(c));
    assert_eq!(evidence.snapshot().phase,Phase::Terminal);
}

#[test]
fn adopt_requires_every_state_grant_byte_and_fresh_success_result() {
    let c=config();let (e,_)=requests(c);
    for at in (56..296).chain([48,51,52,55]) {
        let mut reply=adopt_reply(c);reply[at]^=1;
        let (mut wire,evidence,shared)=backend(reply.clone(),c);
        assert_eq!(wire.baseline().is_err(),true,"byte {at}");
        let before=counts(&shared);
        assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        assert_eq!(wire.baseline().is_err(),true);
        assert_eq!(counts(&shared),before);
        assert_eq!(evidence.snapshot().response.as_bytes(),reply);
        assert_eq!(evidence.snapshot().phase,Phase::Terminal);
    }
}

#[test]
fn malformed_headers_are_bounded_and_never_scan_or_consume_payload() {
    let c=config();let (e,_)=requests(c);
    for kind in [false,true] {
        for at in [0,7,8,11,12,15,16,19,20,23,24,39,40,47] {
            let mut bad=if kind {frame_reply(c,e,2,8,1,0)} else {adopt_reply(c)};
            bad[at]^=1;
            let input=if kind {[adopt_reply(c),bad.clone()].concat()} else {bad.clone()};
            let (mut wire,evidence,shared)=backend(input,c);
            if kind {assert_eq!(wire.baseline(),Ok(baseline(c)));
                assert_eq!(wire.submit(e),BackendOutcome::Unknown);}
            else {assert_eq!(wire.baseline().is_err(),true);}
            let ev=evidence.snapshot();
            assert_eq!(ev.read,48);
            assert_eq!(ev.response.as_bytes(),&bad[..48]);
            assert_eq!(ev.terminal,Some(Fault::Header));
            let before=counts(&shared);assert_eq!(wire.submit(e),BackendOutcome::Unknown);
            assert_eq!(counts(&shared),before);
        }
    }
}

#[test]
fn frame_commit_and_rejection_require_every_echo_command_and_after_byte() {
    let c=config();let (e,_)=requests(c);
    for (outcome,error) in [(1,0),(2,5)] {
        for at in 72..668 {
            let mut bad=frame_reply(c,e,2,8,outcome,error);bad[at]^=1;
            let (mut wire,evidence,shared)=backend([adopt_reply(c),bad.clone()].concat(),c);
            assert_eq!(wire.baseline(),Ok(baseline(c)));
            assert_eq!(wire.submit(e),BackendOutcome::Unknown,"outcome {outcome}, byte {at}");
            assert_eq!(evidence.snapshot().response.as_bytes(),bad);
            assert_eq!(evidence.snapshot().expected,c.initial);
            let before=counts(&shared);assert_eq!(wire.submit(e),BackendOutcome::Unknown);
            assert_eq!(counts(&shared),before);
        }
    }
}

#[test]
fn only_exact_fresh_committed_or_positive_rejected_outcomes_are_definite() {
    let c=config();let (e,_)=requests(c);
    for (outcome,error,api,provenance,operation,reserved) in [
        (1,0,0,0,1,0),(2,5,0,0,1,0),(3,5,0,0,1,0),(1,0,-3,0,1,0),
        (1,0,0,1,1,0),(1,0,0,2,1,0),(2,0,0,0,1,0),(2,-5,0,0,1,0),
        (1,5,0,0,1,0),(0,0,0,0,1,0),(1,0,0,0,2,0),(1,0,0,0,1,1),
    ] {
        let mut reply=frame_reply(c,e,2,8,outcome,error);
        for (at,value) in [(48,api as u32),(52,provenance),(60,operation),(68,reserved)] {
            set32(&mut reply,at,value);
        }
        let (mut wire,evidence,shared)=backend([adopt_reply(c),reply.clone()].concat(),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        let expected=if (api,provenance,operation,reserved)==(0,0,1,0) {
            match (outcome,error) {(1,0)=>committed(e,8),
                (2,n) if n>0=>BackendOutcome::Rejected {request:e,reason:n},
                _=>BackendOutcome::Unknown}
        } else {BackendOutcome::Unknown};
        assert_eq!(wire.submit(e),expected);
        let ev=evidence.snapshot();
        assert_eq!(ev.api_result,Some(api));
        assert_eq!(ev.response.as_bytes(),reply);
        if expected!=committed(e,8) {
            let before=counts(&shared);
            assert_eq!(wire.submit(e),BackendOutcome::Unknown);
            assert_eq!(counts(&shared),before);
            assert_eq!(ev.expected,c.initial);
        }
    }
}

#[test]
fn local_wrong_key_order_and_pristine_submit_are_unattempted_then_terminal() {
    let c=config();let (e,second)=requests(c);let mut other=c;other.producer+=1;
    let (wrong,_)=requests(other);
    for (adopt,request) in [(false,e),(true,wrong),(true,second)] {
        let (mut wire,evidence,shared)=backend(adopt_reply(c),c);
        if adopt {assert_eq!(wire.baseline(),Ok(baseline(c)));}
        let before=counts(&shared);
        assert_eq!(wire.submit(request),BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
        assert_eq!(evidence.snapshot().last_local_refusal,Some(request));
        assert_eq!(wire.submit(request),BackendOutcome::Unknown);
        assert_eq!(wire.baseline().is_err(),true);
        assert_eq!(counts(&shared),before);
    }
}

#[test]
fn counter_boundaries_refuse_before_io_without_wrapping_or_retagging() {
    let c=config();let (e,_)=requests(c);
    // Direct test-only counter placement avoids pretending to execute 2^64 records.
    for counter in 0..5 {
        let (mut wire,evidence,shared)=backend(adopt_reply(c),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        {let mut journal=evidence.0.lock().unwrap();
         match counter {0=>journal.next_exchange=u64::MAX,1=>journal.next_exchange=0,
            2=>journal.next_request=u64::MAX,3=>journal.next_request=0,
            _=>journal.expected.sequence=u64::MAX}}
        let before=counts(&shared);
        assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        assert_eq!(evidence.snapshot().terminal,Some(Fault::Counter));
        assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
    }
    let mut c=c;c.initial.sequence=u64::MAX-1;
    let (e,second)=requests(c);
    let (mut wire,evidence,shared)=backend(replies(c,e),c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    assert_eq!(wire.submit(e),committed(e,u64::MAX));
    assert_eq!(evidence.snapshot().expected.sequence,u64::MAX);
    let before=counts(&shared);
    assert_eq!(wire.submit(second),BackendOutcome::Unknown);
    assert_eq!(counts(&shared),before);
}

#[test]
fn transport_partial_zero_interrupted_and_flush_errors_are_terminal_unknown() {
    let c=config();let (e,_)=requests(c);
    for (stage,after) in [(IoStage::Write,256+19),(IoStage::Flush,1),(IoStage::Read,296+17)] {
        for how in [How::Zero,How::Interrupted,How::Error] {
            if stage==IoStage::Flush && matches!(how,How::Zero) {continue;}
            let (mut script,shared)=Script::new(replies(c,e));
            script.stop=Some(Stop {stage,after,how});
            let (mut wire,evidence)=WireBackend::retain(Box::new(script),c);
            assert_eq!(wire.baseline(),Ok(baseline(c)));
            assert_eq!(wire.submit(e),BackendOutcome::Unknown);
            let before=counts(&shared);let ev=evidence.snapshot();
            assert_eq!(ev.pending.unwrap().as_bytes(),frame_request(c,e,2));
            assert_eq!(ev.phase,Phase::Terminal);
            assert_eq!(ev.expected,c.initial);
            assert_eq!(wire.submit(e),BackendOutcome::Unknown);
            assert_eq!(wire.baseline().is_err(),true);
            assert_eq!(counts(&shared),before);
            if stage==IoStage::Write {assert_eq!(ev.written,19);assert_eq!(ev.flush_started,false);}
            if stage==IoStage::Flush {assert_eq!(ev.written,248);assert_eq!(ev.read,0);
                assert_eq!(ev.flush_started,true);assert_eq!(ev.flush_completed,false);}
            if stage==IoStage::Read {assert_eq!(ev.read,17);assert_eq!(ev.response.len,17);}
        }
    }
}

#[test]
fn eof_at_every_response_boundary_preserves_partial_evidence_without_retry() {
    let c=config();let (e,_)=requests(c);
    for length in [0,1,47,48,49,295,296,467,667] {
        let reply=frame_reply(c,e,2,8,1,0);
        let (mut wire,evidence,shared)=backend([adopt_reply(c),reply[..length].to_vec()].concat(),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        let ev=evidence.snapshot();
        assert_eq!(ev.read,length);
        assert_eq!(ev.response.as_bytes(),&reply[..length]);
        assert_eq!(ev.written,248);
        assert_eq!(ev.flush_completed,true);
        let before=counts(&shared);assert_eq!(wire.submit(e),BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
    }
}

#[test]
fn caught_write_flush_read_panics_leave_armed_uncertainty_and_block_all_reuse() {
    let c=config();let (e,_)=requests(c);
    for (stage,after) in [(IoStage::Write,256+19),(IoStage::Flush,1),(IoStage::Read,296+17)] {
        let (mut script,shared)=Script::new(replies(c,e));
        script.stop=Some(Stop {stage,after,how:How::Panic});
        let (mut wire,evidence)=WireBackend::retain(Box::new(script),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        let panicked=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||wire.submit(e))).is_err();
        let ev=evidence.snapshot();let before=counts(&shared);
        let later=wire.submit(e);
        assert_eq!(panicked,true);
        assert_eq!(ev.phase,Phase::FrameArmed);
        assert_eq!(ev.pending.unwrap().as_bytes(),frame_request(c,e,2));
        assert_eq!(ev.poisoned,false);
        assert_eq!(later,BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
        assert_eq!(wire.baseline().is_err(),true);
        assert_eq!(counts(&shared),before);
    }
}

struct Blocked {
    script: Script, entered: mpsc::SyncSender<()>, release: mpsc::Receiver<()>, blocked: bool,
}
impl Write for Blocked {
    fn write(&mut self,b: &[u8])->io::Result<usize> {
        if !self.blocked {self.blocked=true;self.entered.send(()).unwrap();self.release.recv().unwrap();}
        self.script.write(b)
    }
    fn flush(&mut self)->io::Result<()> {self.script.flush()}
}
impl Read for Blocked {fn read(&mut self,b:&mut [u8])->io::Result<usize>{self.script.read(b)}}

#[test]
fn cloned_evidence_is_available_while_stream_is_blocked_and_records_pre_io_intent() {
    let c=config();
    let (script,shared)=Script::new(adopt_reply(c));
    let (entered,seen)=mpsc::sync_channel(1);let (release,go)=mpsc::sync_channel(1);
    let (mut wire,evidence)=WireBackend::retain(Box::new(Blocked {script,entered,release:go,blocked:false}),c);
    let job=thread::spawn(move || {let result=wire.baseline();(wire,result)});
    seen.recv_timeout(WAIT).unwrap();
    let observe=evidence.clone();let (tx,rx)=mpsc::sync_channel(1);
    let observer=thread::spawn(move || tx.send(observe.snapshot()).unwrap());
    let during=rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let (wire,result)=job.join().unwrap();observer.join().unwrap();
    drop(wire);
    // All workers/barriers are released before any functional assertions.
    assert_eq!(during.is_ok(),true);
    let during=during.unwrap();
    assert_eq!(during.phase,Phase::AdoptArmed);
    assert_eq!(during.pending.unwrap().as_bytes(),adopt_request(c));
    assert_eq!(during.written,0);
    assert_eq!(during.read,0);
    assert_eq!(during.flush_started,false);
    assert_eq!(result,Ok(baseline(c)));
    assert_eq!(counts(&shared).drops,1);
    assert_eq!(evidence.snapshot().phase,Phase::Ready);
}

#[test]
fn unread_suffix_is_not_drained_and_cannot_be_used_as_a_fresh_next_response() {
    let c=config();let (e,_)=requests(c);
    let input=[adopt_reply(c),vec![0xaa],frame_reply(c,e,2,8,1,0)].concat();
    let (mut wire,evidence,shared)=backend(input,c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    assert_eq!(counts(&shared).read,296);
    assert_eq!(wire.submit(e),BackendOutcome::Unknown);
    assert_eq!(counts(&shared).read,296+48);
    assert_eq!(evidence.snapshot().response.bytes[0],0xaa);
    assert_eq!(evidence.snapshot().terminal,Some(Fault::Header));
    let before=counts(&shared);assert_eq!(wire.submit(e),BackendOutcome::Unknown);
    assert_eq!(counts(&shared),before);
}

struct PipeStream { input: ChildStdin, output: ChildStdout, sent: Arc<Mutex<Vec<u8>>> }
impl Write for PipeStream {
    fn write(&mut self,b:&[u8])->io::Result<usize> {
        let n=self.input.write(b)?;self.sent.lock().unwrap().extend_from_slice(&b[..n]);Ok(n)
    }
    fn flush(&mut self)->io::Result<()> {self.input.flush()}
}
impl Read for PipeStream {fn read(&mut self,b:&mut [u8])->io::Result<usize>{self.output.read(b)}}

#[test]
fn real_c_dispatcher_pipe_fixture_preserves_canonical_receipts_and_ambiguous_outcomes() {
    let executable=std::env::var_os("AZMB_WIRE_FIXTURE").expect("explicit private C fixture path");
    let c=config();let (e,_)=requests(c);
    for mode in ["commit","reject","unknown","eof-after-commit","cached","api-error"] {
        let mut child=Command::new(&executable).arg(mode).stdin(Stdio::piped())
            .stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().expect("spawn private fixture");
        let sent=Arc::new(Mutex::new(vec![]));
        let stream=PipeStream {input:child.stdin.take().unwrap(),output:child.stdout.take().unwrap(),sent:sent.clone()};
        let (mut wire,evidence)=WireBackend::retain(Box::new(stream),c);
        let adopted=wire.baseline();let result=wire.submit(e);
        let before=sent.lock().unwrap().clone();
        let repeated=if mode!="commit" {Some(wire.submit(e))} else {None};
        let after=sent.lock().unwrap().clone();let ev=evidence.snapshot();
        drop(wire); // Close pipes; wait for actual process before final oracles.
        let exit=child.wait_with_output().expect("join private fixture");
        assert_eq!(exit.status.success(),true,"mode {mode}: {:?}",exit.stderr);
        assert_eq!(adopted,Ok(baseline(c)));
        let expected=match mode {"commit"=>committed(e,8),
            "reject"=>BackendOutcome::Rejected {request:e,reason:5},_=>BackendOutcome::Unknown};
        assert_eq!(result,expected,"mode {mode}");
        assert_eq!(before,[adopt_request(c),frame_request(c,e,2)].concat());
        assert_eq!(after,before);
        if mode!="commit" {assert_eq!(repeated,Some(BackendOutcome::Unknown));}
        if mode=="commit" {assert_eq!(ev.response.as_bytes(),frame_reply(c,e,2,8,1,0));}
        if mode=="eof-after-commit" {assert_eq!(ev.read,0);assert_eq!(ev.written,248);}
        if mode=="cached" {assert_eq!(ev.provenance,Some(1));}
        if mode=="api-error" {assert_eq!(ev.api_result,Some(-3));assert_eq!(ev.outcome,Some(1));}
    }
}

#[test]
fn previously_committed_duplicate_stays_unknown_without_erasing_positive_evidence_or_resending() {
    let c=config();let (e,_)=requests(c);
    let (mut wire,evidence,shared)=backend(replies(c,e),c);
    assert_eq!(wire.baseline(),Ok(baseline(c)));
    assert_eq!(wire.submit(e),committed(e,8));
    let positive=evidence.snapshot();let before=counts(&shared);
    let duplicate=wire.submit(e);let after=evidence.snapshot();
    assert_eq!(duplicate,BackendOutcome::Unknown);
    assert_eq!(counts(&shared),before);
    assert_eq!(after.pending,positive.pending);
    assert_eq!(after.response,positive.response);
    assert_eq!(after.outcome,Some(1));
    assert_eq!(after.expected,positive.expected);
    assert_eq!(after.last_local_refusal,Some(e));
    assert_eq!(after.phase,Phase::Terminal);
    assert_eq!(wire.submit(e),BackendOutcome::Unknown);
    assert_eq!(counts(&shared),before);
}

#[test]
fn baseline_panic_preserves_adopt_armed_record_and_never_grants_or_retries() {
    let c=config();let (e,_)=requests(c);
    for (stage,after) in [(IoStage::Write,13),(IoStage::Flush,0),(IoStage::Read,17)] {
        let (mut script,shared)=Script::new(adopt_reply(c));
        script.stop=Some(Stop {stage,after,how:How::Panic});
        let (mut wire,evidence)=WireBackend::retain(Box::new(script),c);
        let panicked=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||wire.baseline())).is_err();
        let armed=evidence.snapshot();let before=counts(&shared);
        let submitted=wire.submit(e);let retry=wire.baseline();
        assert_eq!(panicked,true);
        assert_eq!(armed.phase,Phase::AdoptArmed);
        assert_eq!(armed.pending.unwrap().as_bytes(),adopt_request(c));
        assert_eq!(submitted,BackendOutcome::Unknown);
        assert_eq!(retry.is_err(),true);
        assert_eq!(counts(&shared),before);
    }
}

struct ReturnBlocked {
    script: Script, stage: IoStage, entered: mpsc::SyncSender<()>,
    release: mpsc::Receiver<()>, blocked: bool, errors: bool,
}
impl ReturnBlocked {
    fn after(&mut self,stage:IoStage) {
        let counts=counts(&self.script.shared);
        let at_frame=match stage {
            IoStage::Write=>if self.errors {counts.written.len()==256 && counts.calls[0]>1}
                else {counts.written.len()==504},
            IoStage::Flush=>counts.calls[1]==2,
            IoStage::Read=>if self.errors {counts.read==296 && counts.calls[2]>2}
                else {counts.read==964},
        };
        if !self.blocked && self.stage==stage && at_frame {
            self.blocked=true;self.entered.send(()).unwrap();self.release.recv().unwrap();
        }
    }
}
impl Write for ReturnBlocked {
    fn write(&mut self,b:&[u8])->io::Result<usize>{
        let result=self.script.write(b);self.after(IoStage::Write);result
    }
    fn flush(&mut self)->io::Result<()>{
        let result=self.script.flush();self.after(IoStage::Flush);result
    }
}
impl Read for ReturnBlocked {
    fn read(&mut self,b:&mut[u8])->io::Result<usize>{
        let result=self.script.read(b);self.after(IoStage::Read);result
    }
}
#[test]
fn poisoned_journal_salvages_returned_progress_and_complete_reply_without_accepting_it() {
    let c=config();let (e,_)=requests(c);
    for stage in [IoStage::Write,IoStage::Flush,IoStage::Read] {
        let (script,shared)=Script::new(replies(c,e));
        let (entered,seen)=mpsc::sync_channel(1);let (release,go)=mpsc::sync_channel(1);
        let stream=ReturnBlocked {script,stage,entered,release:go,blocked:false,errors:false};
        let (mut wire,evidence)=WireBackend::retain(Box::new(stream),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        let job=thread::spawn(move ||{let outcome=wire.submit(e);(wire,outcome)});
        seen.recv_timeout(WAIT).unwrap();
        let _=std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _guard=evidence.0.lock().unwrap();panic!("injected evidence poison");
        }));
        release.send(()).unwrap();
        let (mut wire,outcome)=job.join().unwrap();
        let after=evidence.snapshot();let before=counts(&shared);
        let repeated=wire.submit(e);
        assert_eq!(outcome,BackendOutcome::Unknown);
        assert_eq!(after.poisoned,true);
        assert_eq!(after.phase,Phase::Terminal);
        assert_eq!(after.terminal,Some(Fault::Poisoned));
        assert_eq!(after.written,248);
        assert_eq!(after.last_io,Some(match stage {
            IoStage::Write=>IoReturn::Written(248),IoStage::Flush=>IoReturn::Flushed,
            IoStage::Read=>IoReturn::Read(620) }));
        if stage!=IoStage::Write {assert_eq!(after.flush_completed,true);}
        if stage==IoStage::Read {
            assert_eq!(after.response.as_bytes(),frame_reply(c,e,2,8,1,0));
            assert_eq!(after.read,668);
            assert_eq!(after.api_result,Some(0));
            assert_eq!(after.outcome,Some(1));
        }
        assert_eq!(after.expected,c.initial);
        assert_eq!(repeated,BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
    }
}

#[test]
fn poisoned_journal_retains_returned_transport_error_without_reclassifying_or_retrying() {
    let c=config();let (e,_)=requests(c);
    for (stage,after) in [(IoStage::Write,256),(IoStage::Flush,1),(IoStage::Read,296)] {
        let (mut script,shared)=Script::new(replies(c,e));
        script.stop=Some(Stop {stage,after,how:How::OsError});
        let (entered,seen)=mpsc::sync_channel(1);let (release,go)=mpsc::sync_channel(1);
        let stream=ReturnBlocked {script,stage,entered,release:go,blocked:false,errors:true};
        let (mut wire,evidence)=WireBackend::retain(Box::new(stream),c);
        assert_eq!(wire.baseline(),Ok(baseline(c)));
        let job=thread::spawn(move ||{let outcome=wire.submit(e);(wire,outcome)});
        seen.recv_timeout(WAIT).unwrap();
        let _=std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _guard=evidence.0.lock().unwrap();panic!("injected return-error journal poison");
        }));
        release.send(()).unwrap();let (mut wire,outcome)=job.join().unwrap();
        let captured=evidence.snapshot();let before=counts(&shared);
        let repeated=wire.submit(e);
        assert_eq!(outcome,BackendOutcome::Unknown);
        assert_eq!(captured.last_io,Some(IoReturn::Error {stage,
            kind:io::Error::from_raw_os_error(5).kind(),raw_os_error:Some(5)}));
        assert_eq!(captured.poisoned,true);
        assert_eq!(captured.phase,Phase::Terminal);
        assert_eq!(captured.expected,c.initial);
        assert_eq!(repeated,BackendOutcome::Unknown);
        assert_eq!(counts(&shared),before);
    }
}
