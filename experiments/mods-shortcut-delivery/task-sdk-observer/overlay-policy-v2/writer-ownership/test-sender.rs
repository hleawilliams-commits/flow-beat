// Actual sender/LatestFrames/wire_frame definitions are extracted by verify.py.
// All transport, sleeping, logging and persistence below are fake. No sockets.
use std::collections::VecDeque;
use std::path::{Path,PathBuf};
use std::sync::{Mutex,OnceLock};
use std::sync::mpsc::{self,Receiver,Sender};
use std::time::Duration;
use std::io;
type Frame = [u8;152];
type LedWire = ();
const BUS_MIXER:u8=2;
#[derive(Clone,Copy)]
pub struct CtrlMsg {bus:u8,frame:Frame}
#[derive(Default)]
struct Fake {connect:VecDeque<bool>, connects:usize, writes:Vec<Vec<u8>>, fail_write:Option<usize>, sleeps:usize, persisted:Vec<u8>}
fn fake()-> &'static Mutex<Fake> {static F:OnceLock<Mutex<Fake>>=OnceLock::new();F.get_or_init(||Mutex::new(Fake::default()))}
struct UnixStream;
impl UnixStream {
 fn connect(_:PathBuf)->io::Result<Self>{let mut f=fake().lock().unwrap();f.connects+=1;assert!(f.connects<5,"bounded fake connect");if f.connect.pop_front().unwrap_or(true){Ok(Self)}else{Err(io::ErrorKind::ConnectionRefused.into())}}
 fn try_clone(&self)->io::Result<Self>{Err(io::ErrorKind::Unsupported.into())}
 fn write_all(&mut self,b:&[u8])->io::Result<()> {let mut f=fake().lock().unwrap();f.writes.push(b.to_vec());if f.fail_write==Some(f.writes.len()){Err(io::ErrorKind::BrokenPipe.into())}else{Ok(())}}
}
mod thread {
 use super::*;
 pub fn sleep(d:Duration){assert_eq!(d,Duration::from_millis(700));fake().lock().unwrap().sleeps+=1;}
 pub fn spawn<F:FnOnce()+Send+'static>(_:F){panic!("fake clone never permits spawned reader");}
}
fn sock_dir()->PathBuf{PathBuf::from("synthetic-no-io")}
fn scratch(_: &str)->PathBuf{PathBuf::from("synthetic-log-no-io")}
fn append_hex(_: &Path,_:&str,_:&[u8]){}
fn read_led_frames(_:UnixStream,_:Sender<LedWire>){panic!("no fake reader");}
fn mixer_levels(frame:&Frame)->Vec<(usize,u8)>{vec![(34,frame[34])]}
fn mixer_state_store(v:&[(usize,u8)]){fake().lock().unwrap().persisted.push(v[0].1);}
fn msg(bus:u8,value:u8)->CtrlMsg {let mut frame=[0u8;152];frame[34]=value;frame[32]=64;CtrlMsg{bus,frame}}
fn reset(connect:&[bool],fail_write:Option<usize>){*fake().lock().unwrap()=Fake{connect:connect.iter().copied().collect(),fail_write,..Default::default()};}
fn run_sender(msgs:&[CtrlMsg]){let(tx,rx)=mpsc::channel();let(led,_)=mpsc::channel();for m in msgs{tx.send(*m).unwrap();}drop(tx);ctrl_thread(rx,led);}
fn observations()->Vec<(u8,u8,u8)> {fake().lock().unwrap().writes.iter().map(|v|(v[0],v[35],v[33])).collect()}
static CHECKS:std::sync::atomic::AtomicUsize=std::sync::atomic::AtomicUsize::new(0);
static FAILURES:std::sync::atomic::AtomicUsize=std::sync::atomic::AtomicUsize::new(0);
fn check(ok:bool,id:&str){CHECKS.fetch_add(1,std::sync::atomic::Ordering::Relaxed);if !ok{FAILURES.fetch_add(1,std::sync::atomic::Ordering::Relaxed);println!("FAIL {id}");}}
fn main(){
 let mut latest=LatestFrames::default();
 check(latest.messages().is_empty(),"latest-starts-empty");
 for b in [3,1,0,2]{latest.note(&msg(b,b+20));}
 check(latest.messages().iter().map(|m|m.bus).collect::<Vec<_>>()==vec![0,1,2,3],"bus-order");
 latest.note(&msg(2,81));latest.note(&msg(99,99));
 check(latest.messages().len()==4,"invalid-bus-not-stored");
 check(latest.messages()[2].frame[34]==81,"latest-replaces-bus");
 let wire=wire_frame(&msg(2,137));
 check(wire.len()==153 && wire[0]==2 && wire[35]==137 && wire[33]==64,"whole-frame-wire");
 reset(&[true],None);run_sender(&[msg(2,71),msg(2,72)]);
 check(observations()==vec![(2,71,64),(2,72,64)],"queued-frames-survive-producer-drop");
 check(fake().lock().unwrap().persisted==vec![71,72],"queued-mixer-persistence");
 check(fake().lock().unwrap().connects==1,"queue-one-connect");
 check(fake().lock().unwrap().sleeps==0,"queue-no-reconnect-sleep");
 reset(&[false,true],None);run_sender(&[msg(2,91),msg(2,92)]);
 check(observations()==vec![(2,92,64)],"disconnected-keeps-latest");
 check(fake().lock().unwrap().persisted==vec![92],"disconnected-persists-replay");
 check(fake().lock().unwrap().connects==2,"disconnected-reconnects");
 check(fake().lock().unwrap().sleeps==1,"disconnected-one-sleep");
 reset(&[true,true],Some(1));run_sender(&[msg(2,111),msg(2,112)]);
 check(observations()==vec![(2,111,64),(2,111,64),(2,112,64)],"uncertain-write-is-replayed");
 check(fake().lock().unwrap().persisted==vec![111,112],"uncertain-success-persistence");
 check(fake().lock().unwrap().connects==2,"uncertain-reconnects");
 check(fake().lock().unwrap().sleeps==0,"write-error-reconnect-has-no-sleep");
 let n=CHECKS.load(std::sync::atomic::Ordering::Relaxed);let f=FAILURES.load(std::sync::atomic::Ordering::Relaxed);
 println!("checks={n} failures={f}");std::process::exit(if f==0{0}else{1});
}
