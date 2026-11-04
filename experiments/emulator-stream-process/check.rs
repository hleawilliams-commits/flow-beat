// SPDX-License-Identifier: GPL-3.0-only
//! Offline process composition fixture. No emulator, native controller endpoint,
//! installation or production process policy is used. The child retains the
//! actual C stream/graph over the exact driver model, while production POSIX
//! stream wrappers execute against two unnamed inherited socketpair ends.
//! Only the data pair belongs to WireBackend; control/Child custody stays here.
//! Fixture timeouts and emergency kill/reap are failure limits, never recovery.
#![forbid(unsafe_code)]
#[allow(dead_code)]
mod mixer_producer;
#[allow(dead_code)]
mod mixer_route;
#[allow(dead_code)]
mod broker_wire;

use broker_wire::{Config, Evidence, Fault, IoStage, Phase, State, WireBackend};
use mixer_producer::{BaselineReceipt, Envelope, FrameAck, Key, Producer};
use mixer_route::{BackendOutcome, MixerBackend};
use std::io::{self, Read, Write};
use std::os::fd::OwnedFd;
use std::os::unix::net::UnixStream;
use std::fs::{self, OpenOptions};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

const LIMIT: Duration = Duration::from_secs(10);
const REPORT_BYTES: usize = 3072;
type CheckResult<T> = Result<T, String>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Mode { Normal, Unknown, LostReply, PartialRequest }
impl Mode {
    fn parse(value: &str) -> Option<Self> {
        match value {
            "normal" => Some(Self::Normal), "unknown" => Some(Self::Unknown),
            "lost-reply" => Some(Self::LostReply), "partial-request" => Some(Self::PartialRequest),
            _ => None,
        }
    }
    fn name(self) -> &'static str {
        match self { Self::Normal => "normal", Self::Unknown => "unknown",
            Self::LostReply => "lost-reply", Self::PartialRequest => "partial-request" }
    }
    fn id(self) -> u32 {
        match self { Self::Normal => 0, Self::Unknown => 1, Self::LostReply => 2, Self::PartialRequest => 3 }
    }
}
fn context<T>(value: io::Result<T>, what: &str) -> CheckResult<T> {
    value.map_err(|error| format!("{what}: {error}"))
}
#[derive(Clone, Default)]
struct Capture(Option<PathBuf>);
impl Capture {
    fn create(path: Option<&Path>) -> CheckResult<Self> {
        if let Some(path) = path {
            context(fs::create_dir(path), "create fresh private evidence directory")?;
        }
        Ok(Self(path.map(Path::to_path_buf)))
    }
    fn bytes(&self, name: &str, bytes: &[u8]) -> CheckResult<()> {
        if let Some(path) = &self.0 {
            let mut file = context(OpenOptions::new().create_new(true).write(true)
                .open(path.join(name)), "create retained evidence file")?;
            context(file.write_all(bytes), "write retained evidence file")?;
        }
        Ok(())
    }
    fn host(&self, name: &str, evidence: &Evidence, counts: &Counts) -> CheckResult<()> {
        self.bytes(&format!("{name}-pending.bin"),
            evidence.pending.as_ref().map_or(&[], |r| r.as_bytes()))?;
        self.bytes(&format!("{name}-response.bin"), evidence.response.as_bytes())?;
        self.bytes(&format!("{name}-written.bin"), &counts.written)?;
        self.bytes(&format!("{name}-received.bin"), &counts.received)?;
        self.bytes(&format!("{name}.txt"), format!("{evidence:#?}\n{counts:#?}\n").as_bytes())
    }
}
fn config() -> Config {
    Config { initial: State {
        instance: [0x1122334455667788, 0xfedcba9876543210],
        run_id: 0x4142434445464748, epoch: 1, sequence: 0,
        flags: 31, crc_len: 96,
        frame: std::array::from_fn(|i| 31u8.wrapping_add((23*i) as u8)),
    }, producer: 99, session: std::array::from_fn(|i| 0x51+i as u8) }
}
fn baseline(c: Config) -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: c.initial.instance, run_id: c.initial.run_id,
        epoch: c.initial.epoch, producer_id: c.producer },
        frame: c.initial.frame, kernel_sequence: c.initial.sequence }
}
fn envelopes(c: Config) -> CheckResult<(Envelope, Envelope)> {
    // These fixture-only value confirmations create immutable inputs. They do
    // not observe controls, establish physical release or activate a route.
    let producer = Producer::new(4).map_err(|e| format!("fixture producer: {e:?}"))?;
    let b = baseline(c);
    producer.stage_baseline(b).map_err(|e| format!("fixture stage: {e:?}"))?;
    producer.acknowledge_baseline(b).map_err(|e| format!("fixture acknowledge: {e:?}"))?;
    producer.confirm_released(b, true).map_err(|e| format!("fixture confirm: {e:?}"))?;
    let one = producer.create_for_key(b.key,
        std::array::from_fn(|i| 97u8.wrapping_add((23*i) as u8)))
        .map_err(|e| format!("fixture request1: {e:?}"))?;
    let two = producer.create_for_key(b.key,
        std::array::from_fn(|i| 143u8.wrapping_add((23*i) as u8)))
        .map_err(|e| format!("fixture request2: {e:?}"))?;
    Ok((one, two))
}

// Append-only full-byte oracles are independent of both production codecs.
// Every CRC-position and tail byte is copied, never normalized.
fn word(out: &mut Vec<u8>, value: u32) { out.extend_from_slice(&value.to_be_bytes()); }
fn wide(out: &mut Vec<u8>, value: u64) { out.extend_from_slice(&value.to_be_bytes()); }
fn header(kind: u32, length: u32, c: Config, exchange: u64) -> Vec<u8> {
    let mut out = b"AZMBRPC1".to_vec();
    for value in [1,kind,length,0] { word(&mut out,value); }
    out.extend_from_slice(&c.session); wide(&mut out,exchange); out
}
fn state_bytes(state: State) -> Vec<u8> {
    let mut out = Vec::new();
    for value in [state.instance[0],state.instance[1],state.run_id,state.epoch,state.sequence] {
        wide(&mut out,value);
    }
    word(&mut out,state.flags); word(&mut out,state.crc_len as u32);
    out.extend_from_slice(&state.frame); out
}
fn request_body(request: Envelope) -> Vec<u8> {
    let key=request.key(); let mut out=Vec::new();
    for value in [key.instance[0],key.instance[1],key.run_id,key.epoch,key.producer_id,request.request_id()] {
        wide(&mut out,value);
    }
    out.extend_from_slice(request.frame()); out
}
fn adopt_request(c: Config) -> Vec<u8> {
    let mut out=header(1,256,c,1);out.extend(state_bytes(c.initial));wide(&mut out,c.producer);out
}
fn adopt_response(c: Config) -> Vec<u8> {
    let mut out=header(0x80000001,296,c,1);
    word(&mut out,0);word(&mut out,0);out.extend(state_bytes(c.initial));
    for value in [c.initial.instance[0],c.initial.instance[1],c.initial.run_id,c.initial.epoch,c.producer] {
        wide(&mut out,value);
    }
    out
}
fn frame_request(c: Config, request: Envelope, exchange: u64) -> Vec<u8> {
    let mut out=header(2,248,c,exchange);out.extend(request_body(request));out
}
fn predicted_state(c: Config, request: Envelope, sequence: u64) -> State {
    State { sequence, frame:*request.frame(), ..c.initial }
}
fn frame_response(c: Config, request: Envelope, exchange: u64, sequence: u64, outcome: u32, error: i32) -> Vec<u8> {
    let mut out=header(0x80000002,668,c,exchange);
    for value in [0,0,outcome,1,error as u32,0] { word(&mut out,value); }
    out.extend(request_body(request));let key=request.key();
    for value in [key.instance[0],key.instance[1],key.run_id,key.epoch,sequence] { wide(&mut out,value); }
    word(&mut out,1);out.extend_from_slice(request.frame());
    out.extend(state_bytes(predicted_state(c,request,sequence)));out
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct Counts {
    writes: usize, reads: usize, flushes: usize, drops: usize,
    written: Vec<u8>, received: Vec<u8>, io_errors: Vec<String>,
}
type SharedCounts=Arc<Mutex<Counts>>;
fn counts(shared: &SharedCounts) -> Counts {
    shared.lock().unwrap_or_else(|error|error.into_inner()).clone()
}
struct DataStream {
    socket: UnixStream,
    counts: SharedCounts,
    partial: bool,
}
impl Write for DataStream {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        let credited={
            let mut c=self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?;
            c.writes+=1;c.written.len()
        };
        if self.partial && credited>=61 {
            return Err(io::Error::other("fixture error after 61 actual socket bytes"));
        }
        let maximum=if self.partial { 61-credited } else { usize::MAX };
        let length=bytes.len().min(17).min(maximum);
        let result=self.socket.write(&bytes[..length]);
        {
            let mut c=self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?;
            match &result {
                Ok(n)=>c.written.extend_from_slice(&bytes[..*n]),
                Err(error)=>c.io_errors.push(format!("write: {error:?}")),
            }
        }
        result
    }
    fn flush(&mut self) -> io::Result<()> {
        self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?.flushes+=1;
        let result=self.socket.flush();
        if let Err(error)=&result {
            self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?
                .io_errors.push(format!("flush: {error:?}"));
        }
        result
    }
}
impl Read for DataStream {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?.reads+=1;
        let length=bytes.len().min(19);
        let result=self.socket.read(&mut bytes[..length]);
        {
            let mut c=self.counts.lock().map_err(|_|io::Error::other("fixture counts poisoned"))?;
            match &result {
                Ok(n)=>c.received.extend_from_slice(&bytes[..*n]),
                Err(error)=>c.io_errors.push(format!("read: {error:?}")),
            }
        }
        result
    }
}
impl Drop for DataStream {
    fn drop(&mut self) {
        self.counts.lock().unwrap_or_else(|error|error.into_inner()).drops+=1;
        // The actual owned UnixStream is then dropped once; no clone remains.
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct PeerRecord {
    fields: [u64;24], request: [u8;256], response: [u8;668],
}
impl PeerRecord {
    fn n(&self,index:usize)->u64 {self.fields[index]}
    fn signed(&self,index:usize)->i64 {self.fields[index] as i64}
    fn bytes_match(&self,request:&[u8],response:&[u8])->bool {
        request.len()<=256 && response.len()<=668 &&
            &self.request[..request.len()]==request &&
            self.request[request.len()..].iter().all(|b|*b==0) &&
            &self.response[..response.len()]==response &&
            self.response[response.len()..].iter().all(|b|*b==0)
    }
}
#[derive(Clone, Debug)]
struct PeerReport {
    meta:[u64;64], model:State, current:PeerRecord, last:PeerRecord,
}
impl PeerReport {
    fn n(&self,index:usize)->u64 {self.meta[index]}
    fn signed(&self,index:usize)->i64 {self.meta[index] as i64}
}
fn u32_at(bytes:&[u8],at:usize)->u32 {
    u32::from_be_bytes(bytes[at..at+4].try_into().expect("fixed validated report offset"))
}
fn u64_at(bytes:&[u8],at:usize)->u64 {
    u64::from_be_bytes(bytes[at..at+8].try_into().expect("fixed validated report offset"))
}
fn decode_record(bytes:&[u8])->CheckResult<PeerRecord> {
    if bytes.len()!=1120 || bytes[1116..].iter().any(|b|*b!=0) {
        return Err("invalid report record padding/length".into());
    }
    if [7,8,9,14,15,20,21].iter().any(|i|u64_at(bytes,i*8)>1) {
        return Err("invalid record boolean domain".into());
    }
    Ok(PeerRecord { fields:std::array::from_fn(|i|u64_at(bytes,i*8)),
        request:bytes[192..448].try_into().expect("fixed request"),
        response:bytes[448..1116].try_into().expect("fixed response") })
}
fn parse_report(bytes:[u8;REPORT_BYTES],kind:u32,mode:Mode)->CheckResult<PeerReport> {
    if &bytes[..8]!=b"AZSPRP01" || u32_at(&bytes,8)!=1 || u32_at(&bytes,12)!=kind ||
        u32_at(&bytes,16)!=mode.id() || u32_at(&bytes,20)!=REPORT_BYTES as u32 ||
        bytes[24..32].iter().any(|b|*b!=0) || bytes[2984..].iter().any(|b|*b!=0) {
        return Err("invalid control report header/padding".into());
    }
    if [0,1,2,7,8,9,11,12,13,16,17,21,22,23,24,25,26,33,34,35,36,37,38,
        41,43,45,46,47,48,50,51,55,56,57].iter()
        .any(|i|u64_at(&bytes,32+i*8)>1) {
        return Err("invalid report boolean domain".into());
    }
    let b=&bytes[544..744];
    let model=State { instance:[u64_at(b,0),u64_at(b,8)],run_id:u64_at(b,16),
        epoch:u64_at(b,24),sequence:u64_at(b,32),flags:u32_at(b,40),
        crc_len:u32_at(b,44) as i32,frame:b[48..200].try_into().expect("fixed state") };
    Ok(PeerReport { meta:std::array::from_fn(|i|u64_at(&bytes,32+i*8)),model,
        current:decode_record(&bytes[744..1864])?,last:decode_record(&bytes[1864..2984])? })
}

struct Fixture {
    child:Child,
    control:UnixStream,
    stderr:Option<JoinHandle<io::Result<(Vec<u8>,usize)>>>,
    waited:bool,
    capture:Capture,
}
impl Fixture {
    fn spawn(executable:&Path,mode:Mode,capture:Capture)->CheckResult<(Self,UnixStream)> {
        let (data_parent,data_child)=context(UnixStream::pair(),"data socketpair")?;
        let (control_parent,control_child)=context(UnixStream::pair(),"control socketpair")?;
        for socket in [&data_parent,&control_parent] {
            context(socket.set_read_timeout(Some(LIMIT)),"read timeout")?;
            context(socket.set_write_timeout(Some(LIMIT)),"write timeout")?;
        }
        let child=context(Command::new(executable).arg(mode.name())
            .stdin(Stdio::from(OwnedFd::from(data_child)))
            .stdout(Stdio::from(OwnedFd::from(control_child)))
            .stderr(Stdio::piped()).spawn(),"spawn private modeled peer")?;
        // Install the real child owner before any readiness/GO/data operation.
        let mut fixture=Self {child,control:control_parent,stderr:None,waited:false,capture};
        let mut stderr=fixture.child.stderr.take().ok_or("missing stderr pipe")?;
        fixture.stderr=Some(context(thread::Builder::new().name("fixture-stderr".into()).spawn(move||{
            let mut retained=Vec::new();let mut total=0usize;let mut buffer=[0u8;1024];
            loop {
                let n=stderr.read(&mut buffer)?;
                if n==0 {break;}
                total=total.saturating_add(n);
                let keep=n.min(4096usize.saturating_sub(retained.len()));
                retained.extend_from_slice(&buffer[..keep]);
            }
            Ok((retained,total))
        }),"stderr drainer")?);
        Ok((fixture,data_parent))
    }
    fn report(&mut self,kind:u32,mode:Mode)->CheckResult<PeerReport> {
        let mut bytes=[0u8;REPORT_BYTES];
        let mut at=0usize;
        let outcome:CheckResult<()>=(|| {
            while at<REPORT_BYTES {
                let n=context(self.control.read(&mut bytes[at..]),"read control report")?;
                if n==0 {return Err("EOF inside fixed control report".into());}
                at+=n;
            }
            Ok(())
        })();
        let name=match kind {1=>"ready.bin",2=>"terminal.bin",3=>"cleanup.bin",_=>"unexpected.bin"};
        // Save actual observed bytes even on partial report, EOF or read error.
        self.capture.bytes(name,&bytes[..at])?;
        outcome?;
        parse_report(bytes,kind,mode)
    }
    fn command(&mut self,value:u8)->CheckResult<()> {
        context(self.control.write_all(&[value]),"write fixture control")
    }
    fn alive(&mut self)->CheckResult<bool> {
        Ok(context(self.child.try_wait(),"child status")?.is_none())
    }
    fn wait(mut self)->CheckResult<()> {
        let deadline=Instant::now()+LIMIT;
        loop {
            if context(self.child.try_wait(),"wait status")?.is_some() {break;}
            if Instant::now()>=deadline {return Err("fixture exit deadline; no successful disposal proof".into());}
            thread::sleep(Duration::from_millis(5));
        }
        let status=context(self.child.wait(),"explicit actual child wait")?;
        self.waited=true;
        let (bytes,total)=self.stderr.take().ok_or("missing stderr drainer")?
            .join().map_err(|_|"stderr drainer panicked")?
            .map_err(|e|format!("stderr drainer: {e}"))?;
        self.capture.bytes("peer-stderr.bin",&bytes)?;
        self.capture.bytes("child-wait.txt",format!("actual_wait=true\nstatus={status}\nstderr_total={total}\n").as_bytes())?;
        if total!=0 {
            return Err(format!("private peer emitted {total} stderr bytes: {}",String::from_utf8_lossy(&bytes)));
        }
        if !status.success() {return Err(format!("unexpected peer exit: {status}"));}
        Ok(())
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        if !self.waited {
            // Fixture failure only: this kills/reaps a private modeled peer.
            // It loses in-memory evidence and never counts as D/disposal.
            eprintln!("FIXTURE_ABORT emergency kill/reap; successful custody proof unavailable");
            let _=self.capture.bytes("emergency-cleanup.txt",
                b"fixture_failure=true\nemergency_kill_wait=true\nproduction_disposal_proven=false\n");
            let _=self.child.kill();
            let _=self.child.wait();
            self.waited=true;
        }
        if let Some(stderr)=self.stderr.take() {
            if let Ok(Ok((bytes,_)))=stderr.join() {
                let _=self.capture.bytes("peer-stderr-emergency.bin",&bytes);
            }
        }
    }
}

fn ready_ok(report:&PeerReport,c:Config)->bool {
    report.n(0)==1 && report.n(1)==1 && report.n(2)==0 && report.signed(3)==0 &&
        report.signed(4)==0 && report.n(5)==1 && report.n(6)==0 && report.n(7)==0 &&
        report.n(8)==1 && report.n(9)==1 && report.signed(10)==0 &&
        report.n(11)==1 && report.n(12)==1 && report.n(13)==0 && report.signed(14)==0 &&
        report.n(16)==0 && report.n(17)==0 && report.n(18)==0 &&
        report.n(20)==4 && (21..=24).all(|i|report.n(i)==1) &&
        report.n(25)==0 && report.n(26)==0 &&
        report.n(27)==5 && report.n(28)==4 && report.n(29)==1 &&
        report.n(30)==0 && report.n(31)==1 && report.n(32)==0 &&
        (33..=37).all(|i|report.n(i)==1) && report.n(38)==0 && report.n(41)==0 &&
        report.n(63)==0 && report.model==c.initial &&
        report.current.n(0)==1 && report.current.n(3)==0 && report.current.n(4)==48 &&
        report.current.bytes_match(&[],&[]) &&
        report.last.fields==[0;24] && report.last.bytes_match(&[],&[])
}
fn held_ok(report:&PeerReport,mode:Mode,frame_count:u64)->bool {
    let lost=mode==Mode::LostReply;
    report.n(0)==1 && report.n(1)==1 && report.n(2)==0 && report.signed(3)==0 &&
        report.signed(4)==(if lost {-7} else {-6}) &&
        report.n(5)==6 && report.n(6)==(if lost {3} else {1}) &&
        report.n(7)==0 && report.n(8)==1 && report.n(9)==1 && report.signed(10)==0 &&
        report.n(11)==1 && report.n(12)==1 &&
        report.signed(14)==(if lost {-7} else {-6}) &&
        report.n(15)==(if lost {3} else {1}) &&
        report.n(16)==0 && report.n(17)==0 && report.n(18)==0 &&
        report.n(20)==4 && (21..=24).all(|i|report.n(i)==1) &&
        report.n(25)==0 && report.n(26)==0 &&
        report.n(27)==(if mode==Mode::PartialRequest {5} else {6+frame_count}) &&
        report.n(28)==(if mode==Mode::PartialRequest {4} else {5}) &&
        report.n(29)==1 && report.n(30)==frame_count && report.n(31)==1 && report.n(32)==0 &&
        (33..=37).all(|i|report.n(i)==1) &&
        report.n(38)==u64::from(lost) && report.signed(39)==0 && report.signed(40)==0 &&
        report.n(41)==0 && report.n(63)>0
}
fn cleanup_ok(report:&PeerReport,held:&PeerReport)->bool {
    // Archived held flags/records describe the pre-disposal owner; actual
    // owner/model liveness and explicit disposal receipts establish cleanup.
    let released_model=State {flags:25,..held.model};
    report.model==released_model && report.n(0)==0 && report.n(1)==1 && report.n(2)==1 &&
        report.meta[3..27]==held.meta[3..27] &&
        report.meta[27..32]==held.meta[27..32] &&
        report.n(32)==2 && report.n(33)==0 && report.n(34)==0 && report.n(35)==0 &&
        report.n(36)==1 && report.n(37)==1 &&
        report.meta[38..41]==held.meta[38..41] &&
        report.n(41)==1 && report.signed(42)==0 && report.n(43)==1 &&
        report.signed(44)==0 && report.n(45)==1 && report.n(46)==0 &&
        report.n(47)==1 && report.n(48)==1 && report.signed(49)==0 &&
        report.n(50)==1 && report.n(51)==1 && report.signed(52)==0 &&
        report.signed(53)==0 && report.signed(54)==0 && report.n(55)==1 &&
        report.n(56)==1 && report.n(57)==1 && report.signed(58)==0 &&
        report.signed(59)==0 && report.signed(60)==0 && report.signed(61)==0 &&
        report.signed(62)==0 && report.n(63)==held.n(63) &&
        report.current==held.current && report.last==held.last
}
fn committed(request:Envelope,sequence:u64)->BackendOutcome {
    BackendOutcome::Committed(FrameAck {request,kernel_sequence:sequence})
}
fn no_resend(before:&Counts,after:&Counts)->bool {before==after}
fn evidence_bytes(e:&Evidence,request:&[u8],response:&[u8])->bool {
    e.pending.as_ref().is_some_and(|pending|pending.as_bytes()==request) &&
        e.response.as_bytes()==response
}
fn completed_read(record:&PeerRecord,length:u64)->bool {
    (2..=length).contains(&record.n(1)) && record.n(8)==1 && record.n(9)==1 &&
        record.n(10)<length && record.n(11)==length-record.n(10) &&
        record.signed(12)>0 && record.n(12)<=record.n(11) &&
        record.n(10).checked_add(record.n(12))==Some(length) && record.signed(13)==0
}
fn completed_write(record:&PeerRecord,length:u64)->bool {
    (1..=length).contains(&record.n(2)) && record.n(14)==1 && record.n(15)==1 &&
        record.n(16)<length && record.n(17)==length-record.n(16) &&
        record.signed(18)>0 && record.n(18)<=record.n(17) &&
        record.n(16).checked_add(record.n(18))==Some(length) && record.signed(19)==0
}
fn completed_record(record:&PeerRecord,number:u64,request:&[u8],response:&[u8])->bool {
    record.n(0)==number && completed_read(record,request.len() as u64) &&
        completed_write(record,response.len() as u64) &&
        record.n(3)==request.len() as u64 && record.n(4)==request.len() as u64 &&
        record.n(5)==response.len() as u64 && record.n(6)==response.len() as u64 &&
        record.n(7)==1 && record.n(8)==1 && record.n(9)==1 &&
        record.n(14)==1 && record.n(15)==1 &&
        record.n(20)==1 && record.n(21)==1 && record.signed(22)==0 &&
        record.n(23)==response.len() as u64 && record.bytes_match(request,response)
}
fn eof_record(record:&PeerRecord,number:u64)->bool {
    record.n(0)==number && record.n(1)==1 && record.n(2)==0 && record.n(3)==0 &&
        record.n(4)==48 && record.n(5)==0 && record.n(6)==0 && record.n(7)==0 &&
        record.n(8)==1 && record.n(9)==1 && record.n(10)==0 && record.n(11)==48 &&
        record.signed(12)==0 && record.signed(13)==0 && record.n(20)==0 &&
        record.fields[14..24]==[0;10] && record.bytes_match(&[],&[])
}

fn run(mode:Mode,executable:&Path,capture:&Capture)->CheckResult<bool> {
    let c=config();let (one,two)=envelopes(c)?;
    let adopt=adopt_request(c);let adopted=adopt_response(c);
    let first=frame_request(c,one,2);let second=frame_request(c,two,3);
    let first_positive=frame_response(c,one,2,1,1,0);
    let first_unknown=frame_response(c,one,2,1,3,5);
    let second_positive=frame_response(c,two,3,2,1,0);
    let (mut fixture,socket)=Fixture::spawn(executable,mode,capture.clone())?;
    let ready=fixture.report(1,mode)?;
    // Semantic readiness is accumulated for the final post-cleanup oracle.
    // It never creates an early assertion/emergency-cleanup control path.
    let alive_while_ready=fixture.alive()?;
    let ready_pass=ready_ok(&ready,c);
    let mut ok=ready_pass && alive_while_ready;
    let shared=Arc::new(Mutex::new(Counts::default()));
    let stream=DataStream {socket,counts:shared.clone(),partial:mode==Mode::PartialRequest};
    let (mut backend,evidence)=WireBackend::retain(Box::new(stream),c);
    fixture.command(b'G')?;
    let baseline_result=backend.baseline();
    let baseline_evidence=evidence.snapshot();
    capture.host("host-baseline",&baseline_evidence,&counts(&shared))?;
    let mut last_outcome=None;
    let mut middle=None;
    let mut reuse_unchanged=true;
    if mode==Mode::PartialRequest {
        let before=counts(&shared);
        let repeated=backend.baseline();
        let reused=backend.submit(one);
        reuse_unchanged=repeated.is_err() && reused==BackendOutcome::Unknown &&
            no_resend(&before,&counts(&shared));
    } else {
        let first_outcome=backend.submit(one);
        let first_evidence=evidence.snapshot();
        capture.host("host-first",&first_evidence,&counts(&shared))?;
        middle=Some((first_outcome,first_evidence));
        if mode==Mode::Normal {
            last_outcome=Some(backend.submit(two));
        } else {
            let before=counts(&shared);
            let reused=backend.submit(one);
            let attempted_baseline=backend.baseline();
            reuse_unchanged=reused==BackendOutcome::Unknown && attempted_baseline.is_err() &&
                no_resend(&before,&counts(&shared));
        }
    }
    let final_evidence=evidence.snapshot();
    let delivered=counts(&shared);
    capture.host("host-final",&final_evidence,&delivered)?;
    drop(backend); // relinquish the single actual data endpoint; guest retains on EOF.
    let dropped=counts(&shared);
    let held=fixture.report(2,mode)?;
    let alive_while_held=fixture.alive()?;
    let frame_count=if mode==Mode::Normal {2} else if mode==Mode::PartialRequest {0} else {1};
    let held_pass=held_ok(&held,mode,frame_count);
    ok=ok && held_pass && alive_while_held &&
        dropped.drops==1 && delivered.drops==0 &&
        dropped.written==delivered.written && dropped.received==delivered.received &&
        reuse_unchanged;
    let custody_pass=ok;
    ok=true;
    match mode {
        Mode::Normal => {
            let first_good=middle.as_ref().is_some_and(|(outcome,e)|*outcome==committed(one,1) &&
                e.phase==Phase::Ready && e.expected==predicted_state(c,one,1) &&
                e.next_request==2 && e.next_exchange==3 &&
                evidence_bytes(e,&first,&first_positive));
            ok=ok && baseline_result==Ok(baseline(c)) &&
                evidence_bytes(&baseline_evidence,&adopt,&adopted) &&
                first_good && last_outcome==Some(committed(two,2)) &&
                final_evidence.phase==Phase::Ready && final_evidence.terminal.is_none() &&
                final_evidence.expected==predicted_state(c,two,2) &&
                final_evidence.next_request==3 && final_evidence.next_exchange==4 &&
                final_evidence.written==248 && final_evidence.read==668 &&
                final_evidence.flush_started && final_evidence.flush_completed &&
                evidence_bytes(&final_evidence,&second,&second_positive) &&
                delivered.written==[adopt.clone(),first.clone(),second.clone()].concat() &&
                delivered.received==[adopted.clone(),first_positive.clone(),second_positive.clone()].concat() &&
                delivered.flushes==3 && delivered.writes>3 && delivered.reads>3 &&
                held.model==predicted_state(c,two,2) && held.n(13)==1 &&
                completed_record(&held.last,3,&second,&second_positive) &&
                eof_record(&held.current,4);
        }
        Mode::Unknown => {
            ok=ok && baseline_result==Ok(baseline(c)) &&
                evidence_bytes(&baseline_evidence,&adopt,&adopted) &&
                middle.as_ref().is_some_and(|(outcome,_)|*outcome==BackendOutcome::Unknown) &&
                final_evidence.phase==Phase::Terminal && final_evidence.terminal==Some(Fault::Unknown) &&
                final_evidence.expected==c.initial && final_evidence.next_request==1 &&
                final_evidence.next_exchange==2 && final_evidence.outcome==Some(3) &&
                final_evidence.written==248 && final_evidence.read==668 &&
                evidence_bytes(&final_evidence,&first,&first_unknown) &&
                delivered.written==[adopt.clone(),first.clone()].concat() &&
                delivered.received==[adopted.clone(),first_unknown.clone()].concat() &&
                delivered.flushes==2 && held.model==predicted_state(c,one,1) &&
                held.n(13)==1 && completed_record(&held.last,2,&first,&first_unknown) &&
                eof_record(&held.current,3);
        }
        Mode::LostReply => {
            let record=&held.current;
            ok=ok && baseline_result==Ok(baseline(c)) &&
                evidence_bytes(&baseline_evidence,&adopt,&adopted) &&
                middle.as_ref().is_some_and(|(outcome,_)|*outcome==BackendOutcome::Unknown) &&
                final_evidence.phase==Phase::Terminal &&
                final_evidence.terminal==Some(Fault::ZeroProgress(IoStage::Read)) &&
                final_evidence.expected==c.initial && final_evidence.next_request==1 &&
                final_evidence.next_exchange==2 && final_evidence.outcome.is_none() &&
                final_evidence.written==248 && final_evidence.read==0 &&
                evidence_bytes(&final_evidence,&first,&[]) &&
                delivered.written==[adopt.clone(),first.clone()].concat() &&
                delivered.received==adopted && delivered.flushes==2 &&
                held.model==predicted_state(c,one,1) && held.n(13)==1 &&
                completed_record(&held.last,1,&adopt,&adopted) &&
                record.n(0)==2 && completed_read(record,248) && record.n(2)==1 &&
                record.n(3)==248 && record.n(4)==248 &&
                record.n(5)==668 && record.n(6)==0 && record.n(7)==1 &&
                record.n(14)==1 && record.n(15)==1 && record.n(16)==0 &&
                record.n(17)==668 && record.signed(18)==-1 && record.signed(19)==32 &&
                record.n(20)==1 && record.n(21)==1 && record.signed(22)==0 &&
                record.n(23)==668 && record.bytes_match(&first,&first_positive);
        }
        Mode::PartialRequest => {
            let record=&held.current;
            ok=ok && baseline_result.is_err() && baseline_evidence.phase==Phase::Terminal &&
                final_evidence.phase==Phase::Terminal &&
                final_evidence.terminal==Some(Fault::Io(IoStage::Write,io::ErrorKind::Other)) &&
                final_evidence.written==61 && final_evidence.read==0 &&
                !final_evidence.flush_started && !final_evidence.flush_completed &&
                evidence_bytes(&final_evidence,&adopt,&[]) &&
                delivered.written==adopt[..61] && delivered.received.is_empty() &&
                delivered.flushes==0 && held.model==c.initial && held.n(13)==0 &&
                held.last.fields==[0;24] && held.last.bytes_match(&[],&[]) &&
                record.n(0)==1 && (3..=62).contains(&record.n(1)) && record.n(2)==0 &&
                record.n(3)==61 && record.n(4)==256 && record.n(5)==0 && record.n(6)==0 &&
                record.n(7)==1 && record.n(8)==1 && record.n(9)==1 &&
                record.n(10)==61 && record.n(11)==195 && record.signed(12)==0 &&
                record.signed(13)==0 && record.fields[14..24]==[0;10] &&
                record.bytes_match(&adopt[..61],&[]);
        }
    }
    let case_pass=ok;
    ok=custody_pass && case_pass;
    // Collect semantic failures above, but always explicitly retire the actual
    // fixture owner and wait before emitting the one functional check.
    fixture.command(b'D')?;
    let cleanup=fixture.report(3,mode)?;
    let cleanup_pass=cleanup_ok(&cleanup,&held);
    ok=cleanup_pass && ok;
    fixture.wait()?;
    capture.bytes("completion.txt",format!(
        "case={}\nready_child_alive={}\nheld_child_alive={}\nready_pass={}\nheld_pass={}\ncustody_pass={}\ncase_pass={}\ncleanup_pass={}\nexited=true\nD_sent=true\ncleanup_received=true\nactual_wait=true\nemergency_cleanup=false\nfunctional_pass={}\nactual_socket_errors={}\n",
        mode.name(),alive_while_ready,alive_while_held,ready_pass,held_pass,custody_pass,case_pass,cleanup_pass,ok,delivered.io_errors.len()).as_bytes())?;
    if !delivered.io_errors.is_empty() {
        return Err(format!("unexpected actual socket errors (including fixture timeouts): {:?}",delivered.io_errors));
    }
    Ok(ok)
}
fn main() {
    let args:Vec<_>=std::env::args_os().collect();
    if !(3..=4).contains(&args.len()) {
        eprintln!("usage: check <normal|unknown|lost-reply|partial-request> <peer-path> [fresh-private-evidence-dir]");
        std::process::exit(2);
    }
    let Some(mode)=args[1].to_str().and_then(Mode::parse) else {
        eprintln!("invalid selector");std::process::exit(2);
    };
    let capture=match Capture::create(args.get(3).map(Path::new)) {
        Ok(capture)=>capture,
        Err(error)=>{eprintln!("FIXTURE_FAILURE {error}");std::process::exit(2);}
    };
    match run(mode,Path::new(&args[2]),&capture) {
        Ok(good) => {
            if !good {eprintln!("FAIL process-{}",mode.name());}
            println!("checks=1 failures={}",usize::from(!good));
            std::process::exit(i32::from(!good));
        }
        Err(error) => {
            let _=capture.bytes("fixture-failure.txt",format!("{error}\n").as_bytes());
            eprintln!("FIXTURE_FAILURE {error}");
            std::process::exit(2);
        }
    }
}
