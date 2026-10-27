// SPDX-License-Identifier: GPL-2.0-only
//! Prospective std-only producer queue gate. No transport, ioctl or host I/O.
//! Receipt authenticity and physical/UI release observations belong to a future
//! trusted broker adapter. Private request fields prevent mutation/retagging
//! through this API; they do not establish the provenance of caller frame bytes.
#![forbid(unsafe_code)]
use std::collections::VecDeque;
use std::sync::{Mutex, MutexGuard};

pub const FRAME_BYTES: usize = 152;
pub const MAX_QUEUE: usize = 64;
pub type Frame = [u8; FRAME_BYTES];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    pub instance: [u64; 2],
    pub run_id: u64,
    pub epoch: u64,
    pub producer_id: u64,
}
impl Key {
    fn valid(self) -> bool {
        self.instance != [0, 0] && self.run_id != 0 && self.epoch != 0 && self.producer_id != 0
    }
}

/// Explicit broker adoption/readback value, not a self-authenticating capability.
/// `kernel_sequence` may be nonzero when adopting an existing owned state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BaselineReceipt {
    pub key: Key,
    pub frame: Frame,
    pub kernel_sequence: u64,
}

/// Immutable copy made under the gate lock at input creation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Envelope {
    key: Key,
    request_id: u64,
    frame: Frame,
}
impl Envelope {
    pub fn key(&self) -> Key { self.key }
    pub fn request_id(&self) -> u64 { self.request_id }
    pub fn frame(&self) -> &Frame { &self.frame }
}

/// A separate positive FRAME receipt; socket success is not this receipt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameAck {
    pub request: Envelope,
    pub kernel_sequence: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidCapacity, InvalidKey, Poisoned, Closed, Busy, QueueFull, Empty,
    StaleKey, LifecycleChange, NotNewEpoch, OutOfOrder, Conflict,
    NoBaseline, BaselineMismatch, BaselineUnacknowledged, HeldControls,
    NoUnresolved, WrongAck, Overflow,
}
#[derive(Debug, PartialEq, Eq)]
pub enum SettingsError<E> { Gate(Error), Callback(E) }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Pending {
    pub request: Envelope,
    pub uncertain: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Status {
    pub admission_open: bool,
    pub staged: bool,
    pub baseline_acknowledged: bool,
    pub last_key: Option<Key>,
    pub queued: usize,
    pub unresolved: Option<Pending>,
    pub cached: Option<Envelope>,
    pub next_request_id: u64,
    pub kernel_sequence: u64,
}

struct Inner {
    open: bool,
    last_key: Option<Key>,
    staged: Option<(BaselineReceipt, bool)>,
    queue: VecDeque<Envelope>,
    unresolved: Option<Pending>,
    cached: Option<Envelope>,
    next_request_id: u64,
    kernel_sequence: u64,
}
impl Inner {
    // Never erase an unresolved native/transport operation on closure.
    fn close(&mut self) {
        self.open = false;
        self.staged = None;
        self.queue.clear();
        self.cached = None;
    }
}

/// All creation, admission, closure and host-settings callbacks serialize here.
/// The caller must NOT invoke gate methods recursively from a settings callback.
/// A callback panic poisons the lock; every subsequent method fails closed.
/// Destruction provides no broker cancellation or host/overlay recovery action.
pub struct Producer {
    capacity: usize,
    inner: Mutex<Inner>,
}
impl Producer {
    pub fn new(capacity: usize) -> Result<Self, Error> {
        if capacity == 0 || capacity > MAX_QUEUE { return Err(Error::InvalidCapacity); }
        Ok(Self { capacity, inner: Mutex::new(Inner {
            open: false, last_key: None, staged: None,
            queue: VecDeque::with_capacity(capacity), unresolved: None, cached: None,
            next_request_id: 1, kernel_sequence: 0,
        }) })
    }
    fn lock(&self) -> Result<MutexGuard<'_, Inner>, Error> {
        self.inner.lock().map_err(|_| Error::Poisoned)
    }
    /// Stage an externally verified broker baseline, remaining closed. Any old
    /// unresolved operation blocks staging. Reuse requires the same instance/run
    /// and a strictly newer epoch, including after a rejected/abandoned staging.
    pub fn stage_baseline(&self, baseline: BaselineReceipt) -> Result<(), Error> {
        let mut s = self.lock()?;
        if !baseline.key.valid() { return Err(Error::InvalidKey); }
        if s.unresolved.is_some() || s.open { return Err(Error::Busy); }
        if let Some(old) = s.last_key {
            if baseline.key.instance != old.instance || baseline.key.run_id != old.run_id {
                return Err(Error::LifecycleChange);
            }
            if baseline.key.epoch <= old.epoch { return Err(Error::NotNewEpoch); }
        }
        // The C broker refuses an operation without a nonwrapping next sequence.
        if baseline.kernel_sequence == u64::MAX { return Err(Error::Overflow); }
        s.close();
        s.last_key = Some(baseline.key);
        s.kernel_sequence = baseline.kernel_sequence;
        s.next_request_id = 1;
        s.staged = Some((baseline, false));
        Ok(())
    }
    /// Only a complete exact broker adoption ACK advances this stage. Mismatch
    /// invalidates the staging; identical values alone cannot authenticate ACKs.
    pub fn acknowledge_baseline(&self, observed: BaselineReceipt) -> Result<(), Error> {
        let mut s = self.lock()?;
        let (expected, _) = s.staged.ok_or(Error::NoBaseline)?;
        if expected != observed {
            s.close();
            return Err(Error::BaselineMismatch);
        }
        s.staged = Some((expected, true));
        Ok(())
    }
    /// Caller asserts every relevant UI/physical control has been released after
    /// acknowledged baseline adoption. This module has no hardware observation.
    pub fn confirm_released(&self, baseline: BaselineReceipt, released: bool) -> Result<(), Error> {
        let mut s = self.lock()?;
        let (expected, acked) = s.staged.ok_or(Error::NoBaseline)?;
        if expected != baseline {
            s.close();
            return Err(Error::BaselineMismatch);
        }
        if !acked { return Err(Error::BaselineUnacknowledged); }
        if !released { return Err(Error::HeldControls); }
        s.staged = None;
        s.open = true;
        Ok(())
    }
    /// Capture the current key and reserve a contiguous ID together with a
    /// bounded canonical queue entry. A full queue consumes no request ID.
    /// The caller attaches this envelope before any external mpsc/socket queue.
    pub fn create(&self, frame: Frame) -> Result<Envelope, Error> {
        let mut s = self.lock()?;
        if !s.open { return Err(Error::Closed); }
        if s.queue.len() + usize::from(s.unresolved.is_some()) >= self.capacity {
            return Err(Error::QueueFull);
        }
        if s.next_request_id == u64::MAX || s.kernel_sequence == u64::MAX {
            s.close();
            return Err(Error::Overflow);
        }
        let request = Envelope { key: s.last_key.ok_or(Error::Closed)?,
            request_id: s.next_request_id, frame };
        s.next_request_id += 1;
        s.queue.push_back(request);
        s.cached = Some(request);
        Ok(request)
    }
    /// Admit a delivered immutable envelope, once, only in canonical queue order.
    /// A dropped external message cannot let a later ID skip the broker's next ID.
    /// Dequeue never creates or replaces a key. At most one operation is unresolved.
    pub fn admit(&self, delivered: Envelope) -> Result<Envelope, Error> {
        let mut s = self.lock()?;
        if !s.open { return Err(Error::Closed); }
        if s.last_key != Some(delivered.key) { return Err(Error::StaleKey); }
        if s.unresolved.is_some() { return Err(Error::Busy); }
        if s.kernel_sequence == u64::MAX {
            s.close();
            return Err(Error::Overflow);
        }
        let front = *s.queue.front().ok_or(Error::Empty)?;
        if front.request_id != delivered.request_id { return Err(Error::OutOfOrder); }
        if front != delivered { s.close(); return Err(Error::Conflict); }
        s.queue.pop_front();
        s.unresolved = Some(Pending { request: front, uncertain: false });
        Ok(front)
    }
    /// A transport enqueue/write success does not resolve or reopen anything.
    pub fn transport_written(&self, request: Envelope) -> Result<(), Error> {
        let s = self.lock()?;
        if s.unresolved.map(|p| p.request) != Some(request) { return Err(Error::NoUnresolved); }
        Ok(())
    }
    /// Reconciliation can acknowledge an exact retained request even after
    /// closure. It never reopens admission, requeues work or clears another ID.
    pub fn acknowledge_frame(&self, ack: FrameAck) -> Result<(), Error> {
        let mut s = self.lock()?;
        let pending = s.unresolved.ok_or(Error::NoUnresolved)?;
        if pending.request != ack.request || s.kernel_sequence.checked_add(1) != Some(ack.kernel_sequence) {
            s.close();
            return Err(Error::WrongAck);
        }
        s.kernel_sequence = ack.kernel_sequence;
        s.unresolved = None;
        if s.kernel_sequence == u64::MAX || ack.request.request_id == u64::MAX - 1 {
            s.close();
        }
        Ok(())
    }
    /// Only an exact definitive broker rejection may resolve as not committed.
    /// An I/O error or old STATE snapshot is not a definitive rejection. Require
    /// a newer verified generation before admitting another input after rejection.
    pub fn reject_known(&self, request: Envelope) -> Result<(), Error> {
        let mut s = self.lock()?;
        if s.unresolved.map(|p| p.request) != Some(request) {
            s.close();
            return Err(Error::WrongAck);
        }
        s.close();
        s.unresolved = None;
        Ok(())
    }
    /// Socket loss has unknown outcome. Retain exactly the original request for
    /// a query; discard all unsent queue/cache data and never automatically retry.
    pub fn connection_lost(&self) -> Result<(), Error> {
        let mut s = self.lock()?;
        s.close();
        if let Some(pending) = &mut s.unresolved { pending.uncertain = true; }
        Ok(())
    }
    /// Local quiescence only: not proof of a drained UI/transport/native queue.
    pub fn close_generation(&self) -> Result<(), Error> {
        self.lock()?.close();
        Ok(())
    }
    /// Serialize a future settings side effect with gate transitions. The
    /// captured key must come from input creation, not be refreshed at dequeue.
    /// Error closes admission; it cannot undo any partial effect of the callback.
    pub fn with_host_settings<T, E, F>(&self, created_key: Key, f: F) -> Result<T, SettingsError<E>>
    where F: FnOnce() -> Result<T, E> {
        let mut s = self.lock().map_err(SettingsError::Gate)?;
        if !s.open { return Err(SettingsError::Gate(Error::Closed)); }
        if s.last_key != Some(created_key) { return Err(SettingsError::Gate(Error::StaleKey)); }
        match f() {
            Ok(value) => Ok(value),
            Err(error) => { s.close(); Err(SettingsError::Callback(error)) }
        }
    }
    pub fn inspect(&self) -> Result<Status, Error> {
        let s = self.lock()?;
        Ok(Status { admission_open: s.open, staged: s.staged.is_some(),
            baseline_acknowledged: s.staged.map(|(_, acked)| acked).unwrap_or(false),
            last_key: s.last_key, queued: s.queue.len(), unresolved: s.unresolved,
            cached: s.cached, next_request_id: s.next_request_id,
            kernel_sequence: s.kernel_sequence })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{mpsc, Arc};
    use std::thread;
    fn key(epoch: u64) -> Key { Key { instance: [0x1234, 0x5678], run_id: 7, epoch, producer_id: 19 } }
    fn frame(seed: u8) -> Frame { std::array::from_fn(|n| seed.wrapping_add((n as u8).wrapping_mul(23))) }
    fn baseline(epoch: u64) -> BaselineReceipt { BaselineReceipt { key: key(epoch), frame: frame(3), kernel_sequence: 4 } }
    fn arm(p: &Producer, b: BaselineReceipt) { p.stage_baseline(b).unwrap(); p.acknowledge_baseline(b).unwrap(); p.confirm_released(b, true).unwrap(); }
    fn producer(n: usize) -> Producer { let p=Producer::new(n).unwrap(); arm(&p,baseline(1)); p }
    fn ack(p: &Producer, e: Envelope) { let seq=p.inspect().unwrap().kernel_sequence+1; p.acknowledge_frame(FrameAck { request:e, kernel_sequence:seq }).unwrap(); }

    #[test]
    fn default_closed_and_bounded_capacity() {
        assert!(matches!(Producer::new(0),Err(Error::InvalidCapacity)));
        assert!(matches!(Producer::new(MAX_QUEUE+1),Err(Error::InvalidCapacity)));
        let p=Producer::new(1).unwrap(); assert_eq!(p.create(frame(1)),Err(Error::Closed));
        assert!(!p.inspect().unwrap().admission_open);
        assert_eq!(p.acknowledge_baseline(baseline(1)),Err(Error::NoBaseline));
    }
    #[test]
    fn exact_adoption_ack_and_release_required() {
        let p=Producer::new(2).unwrap(); let b=baseline(1); p.stage_baseline(b).unwrap();
        assert_eq!(p.create(frame(1)),Err(Error::Closed));
        assert_eq!(p.confirm_released(b,true),Err(Error::BaselineUnacknowledged));
        p.acknowledge_baseline(b).unwrap(); assert_eq!(p.confirm_released(b,false),Err(Error::HeldControls));
        assert_eq!(p.create(frame(1)),Err(Error::Closed)); p.confirm_released(b,true).unwrap();
        assert_eq!(p.create(frame(1)).unwrap().request_id(),1);
        assert_eq!(p.confirm_released(b,true),Err(Error::NoBaseline));
    }
    #[test]
    fn every_baseline_component_is_bound() {
        for which in 0..7 {
            let p=Producer::new(1).unwrap(); let b=baseline(1); p.stage_baseline(b).unwrap(); let mut bad=b;
            match which { 0=>bad.key.instance[0]^=1,1=>bad.key.instance[1]^=1,2=>bad.key.run_id+=1,
                3=>bad.key.epoch+=1,4=>bad.key.producer_id+=1,5=>bad.kernel_sequence+=1,_=>bad.frame[151]^=1 }
            assert_eq!(p.acknowledge_baseline(bad),Err(Error::BaselineMismatch));
            assert!(!p.inspect().unwrap().admission_open); assert_eq!(p.acknowledge_baseline(b),Err(Error::NoBaseline));
        }
    }
    #[test]
    fn key_validity_and_no_same_epoch_rearm() {
        for which in 0..4 { let p=Producer::new(1).unwrap(); let mut b=baseline(1);
            match which {0=>b.key.instance=[0,0],1=>b.key.run_id=0,2=>b.key.epoch=0,_=>b.key.producer_id=0}
            assert_eq!(p.stage_baseline(b),Err(Error::InvalidKey)); }
        let p=producer(2); p.close_generation().unwrap();
        assert_eq!(p.stage_baseline(baseline(1)),Err(Error::NotNewEpoch));
        let mut b=baseline(2); b.key.run_id+=1; assert_eq!(p.stage_baseline(b),Err(Error::LifecycleChange));
        b=baseline(2); b.key.instance[1]^=1; assert_eq!(p.stage_baseline(b),Err(Error::LifecycleChange));
        arm(&p,baseline(2)); assert!(p.inspect().unwrap().admission_open);
    }
    #[test]
    fn full_queue_and_lost_message_do_not_skip_ids() {
        let p=producer(2); let one=p.create(frame(1)).unwrap(); let two=p.create(frame(2)).unwrap();
        assert_eq!(p.create(frame(9)),Err(Error::QueueFull));
        assert_eq!(p.inspect().unwrap().next_request_id,3);
        assert_eq!(p.admit(two),Err(Error::OutOfOrder));
        assert_eq!(p.inspect().unwrap().queued,2); p.admit(one).unwrap();
        assert_eq!(p.admit(two),Err(Error::Busy)); assert_eq!(p.create(frame(9)),Err(Error::QueueFull));
        ack(&p,one); let three=p.create(frame(3)).unwrap(); assert_eq!(three.request_id(),3);
        p.admit(two).unwrap(); ack(&p,two); p.admit(three).unwrap(); ack(&p,three);
        assert_eq!(p.admit(one),Err(Error::Empty));
    }
    #[test]
    fn no_enqueue_or_socket_success_ack() {
        let p=producer(2); let one=p.create(frame(1)).unwrap();
        assert!(p.inspect().unwrap().unresolved.is_none()); p.admit(one).unwrap();
        p.transport_written(one).unwrap(); p.transport_written(one).unwrap();
        assert_eq!(p.inspect().unwrap().unresolved.unwrap().request,one);
        let two=p.create(frame(2)).unwrap(); assert_eq!(p.admit(two),Err(Error::Busy));
        ack(&p,one); assert_eq!(p.acknowledge_frame(FrameAck{request:one,kernel_sequence:5}),Err(Error::NoUnresolved));
        p.admit(two).unwrap();
    }
    #[test]
    fn every_frame_ack_component_is_bound() {
        for which in 0..8 {
            let p=producer(2); let one=p.create(frame(1)).unwrap(); p.admit(one).unwrap();
            let mut a=FrameAck{request:one,kernel_sequence:5};
            match which {0=>a.request.key.instance[0]^=1,1=>a.request.key.instance[1]^=1,2=>a.request.key.run_id+=1,
                3=>a.request.key.epoch+=1,4=>a.request.key.producer_id+=1,5=>a.request.request_id+=1,
                6=>a.request.frame[151]^=1,_=>a.kernel_sequence+=1}
            assert_eq!(p.acknowledge_frame(a),Err(Error::WrongAck));
            assert!(!p.inspect().unwrap().admission_open); assert_eq!(p.inspect().unwrap().unresolved.unwrap().request,one);
            ack(&p,one); assert!(!p.inspect().unwrap().admission_open);
        }
    }
    #[test]
    fn loss_retains_unknown_no_replay_or_adoption() {
        let p=producer(3); let one=p.create(frame(1)).unwrap(); p.admit(one).unwrap(); let two=p.create(frame(2)).unwrap();
        p.connection_lost().unwrap(); let s=p.inspect().unwrap();
        assert_eq!(s.unresolved,Some(Pending{request:one,uncertain:true})); assert_eq!(s.queued,0); assert_eq!(s.cached,None);
        assert_eq!(p.admit(one),Err(Error::Closed)); assert_eq!(p.admit(two),Err(Error::Closed));
        assert_eq!(p.stage_baseline(baseline(2)),Err(Error::Busy)); p.close_generation().unwrap();
        assert_eq!(p.stage_baseline(baseline(2)),Err(Error::Busy));
        ack(&p,one); assert!(!p.inspect().unwrap().admission_open); arm(&p,baseline(2));
        assert_eq!(p.create(frame(7)).unwrap().request_id(),1);
    }
    #[test]
    fn exact_known_rejection_closes_but_can_transition() {
        let p=producer(2); let one=p.create(frame(1)).unwrap(); p.admit(one).unwrap(); p.create(frame(2)).unwrap();
        p.reject_known(one).unwrap(); let s=p.inspect().unwrap();
        assert!(!s.admission_open); assert!(s.unresolved.is_none()); assert_eq!(s.queued,0);
        assert_eq!(s.kernel_sequence,4); assert_eq!(p.stage_baseline(baseline(1)),Err(Error::NotNewEpoch));
        arm(&p,baseline(2));
    }
    #[test]
    fn stale_mpsc_and_cached_replay_cannot_retag() {
        let p=Arc::new(producer(3)); let old=p.create(frame(1)).unwrap(); let cached=p.inspect().unwrap().cached.unwrap();
        let (tx,rx)=mpsc::sync_channel(1); let (go_tx,go_rx)=mpsc::channel();
        let t=thread::spawn(move||{go_rx.recv().unwrap();tx.send(old).unwrap();});
        p.close_generation().unwrap(); assert_eq!(p.inspect().unwrap().cached,None); arm(&p,baseline(2));
        let new=p.create(frame(1)).unwrap(); assert_eq!(new.request_id(),1); assert_ne!(new.key(),old.key());
        go_tx.send(()).unwrap(); let delivered=rx.recv().unwrap();
        assert_eq!(p.admit(delivered),Err(Error::StaleKey)); assert_eq!(p.admit(cached),Err(Error::StaleKey));
        assert_eq!(p.inspect().unwrap().queued,1); p.admit(new).unwrap(); ack(&p,new); t.join().unwrap();
    }
    #[test]
    fn altered_same_id_is_conflict_not_replacement() {
        let p=producer(2); let one=p.create(frame(1)).unwrap(); let mut forged=one; forged.frame[0]^=1;
        assert_eq!(p.admit(forged),Err(Error::Conflict)); assert!(!p.inspect().unwrap().admission_open);
        assert_eq!(p.inspect().unwrap().queued,0);
    }
    #[test]
    fn stale_or_failed_settings_callback_is_gated() {
        let p=producer(2); let one=p.create(frame(1)).unwrap(); let mut calls=0;
        assert_eq!(p.with_host_settings(one.key(),||{calls+=1;Ok::<_,()>(7)}),Ok(7)); assert_eq!(calls,1);
        p.close_generation().unwrap(); arm(&p,baseline(2));
        assert_eq!(p.with_host_settings(one.key(),||{calls+=1;Ok::<_,()>(())}),Err(SettingsError::Gate(Error::StaleKey)));
        assert_eq!(calls,1); let two=p.create(frame(2)).unwrap(); p.admit(two).unwrap();
        assert_eq!(p.with_host_settings(two.key(),||{calls+=1;Err::<(),_>("partial")}),Err(SettingsError::Callback("partial")));
        assert_eq!(calls,2); assert!(!p.inspect().unwrap().admission_open); assert_eq!(p.inspect().unwrap().unresolved.unwrap().request,two);
    }
    #[test]
    fn settings_and_closure_share_the_transition_lock() {
        let p=Arc::new(producer(2)); let e=p.create(frame(1)).unwrap();
        let (entered_tx,entered_rx)=mpsc::channel(); let (finish_tx,finish_rx)=mpsc::channel();
        let callback_p=p.clone(); let t=thread::spawn(move||callback_p.with_host_settings(e.key(),||{entered_tx.send(()).unwrap();finish_rx.recv().unwrap();Ok::<_,()>(())}));
        entered_rx.recv().unwrap(); assert!(matches!(p.inner.try_lock(),Err(std::sync::TryLockError::WouldBlock)));
        let (closed_tx,closed_rx)=mpsc::channel();let close_p=p.clone();let closer=thread::spawn(move||{close_p.close_generation().unwrap();closed_tx.send(()).unwrap();});
        assert!(matches!(closed_rx.try_recv(),Err(mpsc::TryRecvError::Empty))); finish_tx.send(()).unwrap();
        t.join().unwrap().unwrap(); closed_rx.recv().unwrap(); closer.join().unwrap(); assert!(!p.inspect().unwrap().admission_open);
    }
    #[test]
    fn callback_panic_poison_fails_closed() {
        let p=producer(1);let e=p.create(frame(1)).unwrap();
        let caught=std::panic::catch_unwind(||{let _:Result<(),SettingsError<()>>=p.with_host_settings(e.key(),||panic!("fixture callback"));});
        assert!(caught.is_err()); assert_eq!(p.create(frame(2)),Err(Error::Poisoned)); assert_eq!(p.inspect(),Err(Error::Poisoned));
    }
    #[test]
    fn counters_never_wrap() {
        let p=producer(2);p.inner.lock().unwrap().next_request_id=u64::MAX-1;
        let one=p.create(frame(1)).unwrap(); assert_eq!(one.request_id(),u64::MAX-1);p.admit(one).unwrap();ack(&p,one);
        assert_eq!(p.create(frame(2)),Err(Error::Closed)); assert_eq!(p.inspect().unwrap().next_request_id,u64::MAX);
        let p=Producer::new(2).unwrap();let mut b=baseline(1);b.kernel_sequence=u64::MAX;
        assert_eq!(p.stage_baseline(b),Err(Error::Overflow));b.kernel_sequence=u64::MAX-1;arm(&p,b);
        let one=p.create(frame(1)).unwrap();p.admit(one).unwrap();ack(&p,one);assert_eq!(p.create(frame(2)),Err(Error::Closed));
    }
    #[test]
    fn queued_work_refuses_after_kernel_sequence_exhaustion() {
        let p=Producer::new(2).unwrap(); let mut b=baseline(1); b.kernel_sequence=u64::MAX-1; arm(&p,b);
        let one=p.create(frame(1)).unwrap(); let two=p.create(frame(2)).unwrap(); p.admit(one).unwrap(); ack(&p,one);
        assert_eq!(p.with_host_settings(b.key,||Ok::<_,()>(())),Err(SettingsError::Gate(Error::Closed)));
        assert_eq!(p.admit(two),Err(Error::Closed)); assert!(!p.inspect().unwrap().admission_open);
        assert_eq!(p.inspect().unwrap().queued,0);
    }
    #[test]
    fn same_generation_settings_order_is_an_explicit_limit() {
        let p=producer(2); let first=p.create(frame(1)).unwrap(); let second=p.create(frame(2)).unwrap(); let mut saved=0;
        p.with_host_settings(second.key(),||{saved=2;Ok::<_,()>(())}).unwrap();
        p.with_host_settings(first.key(),||{saved=1;Ok::<_,()>(())}).unwrap();
        assert_eq!(saved,1); assert_eq!(p.inspect().unwrap().queued,2);
    }
    #[test]
    fn reauthoring_old_bytes_is_an_explicit_limit() {
        let p=producer(2); let old=p.create(frame(8)).unwrap(); p.close_generation().unwrap(); arm(&p,baseline(2));
        let reauthored=p.create(*old.frame()).unwrap(); assert_eq!(reauthored.frame(),old.frame());
        assert_ne!(reauthored.key(),old.key()); assert_eq!(reauthored.request_id(),1);
        p.admit(reauthored).unwrap(); ack(&p,reauthored);
    }
    #[test]
    fn concurrent_creation_assigns_one_contiguous_queue() {
        let p=Arc::new(producer(16));let barrier=Arc::new(std::sync::Barrier::new(9));let mut threads=Vec::new();
        for n in 0..8{let p=p.clone();let b=barrier.clone();threads.push(thread::spawn(move||{b.wait();p.create(frame(n)).unwrap()}));}
        barrier.wait();let mut messages:Vec<_>=threads.into_iter().map(|t|t.join().unwrap()).collect();messages.sort_by_key(|e|e.request_id());
        assert_eq!(messages.iter().map(|e|e.request_id()).collect::<Vec<_>>(),(1..=8).collect::<Vec<_>>());
        for e in messages{p.admit(e).unwrap();ack(&p,e);}assert_eq!(p.inspect().unwrap().kernel_sequence,12);
    }
}
