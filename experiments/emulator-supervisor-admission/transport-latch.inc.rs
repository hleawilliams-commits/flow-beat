
// Additive inactive supervisor-admission seam. Existing transport policy stays
// unchanged: this exposes only the same terminal atomic stop plus try_send wake.
impl MixerPort {
    /// Permanently closes port admission without taking mailbox/route/settings
    /// locks. The returned first reason may predate this call. No queue-drained,
    /// native cancellation, settings-suppression or joined receipt is implied.
    /// A reserved request racing the worker's final terminal check may proceed.
    pub fn latch_terminal_admission(&self) -> StopReason {
        let shared = self.shared();
        shared.stop(StopReason::Requested);
        shared.reason()
    }
}
