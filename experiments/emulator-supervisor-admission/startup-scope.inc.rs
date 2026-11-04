
// Additive scoped access to the actual retained Child. No raw Child or supervisor
// Arc is exported, and the frozen generic supervisor custody graph is unchanged.
impl StartupHandle<std::process::Child> {
    /// Caller-owned journals bind permanently for this scope. The exact retained
    /// child stays under its existing supervisor mutex for the whole callback.
    /// Both normal return and unwind terminal-latch the exact lifecycle port.
    /// This does not finish/join, close stdin, stop the child or authorize native
    /// cleanup. The operation lease excludes startup/handle.finish, not a caller
    /// concurrently invoking the already-returned LifecycleControl directly.
    /// Reentering this scope returns Busy; supervisor poison is never recovered
    /// operationally. An entry refusal runs no callback. Once the exact port is
    /// available, missing/busy/poisoned supervisor refusal consumes the admission
    /// journal and attempts one terminal latch without binding/polling the child.
    /// Earlier refusal has no closure claim. A post-scope latch failure preserves
    /// its journal and returns Err after callback completion; it does not claim
    /// successful terminal closure.
    pub fn with_supervisor_observation<R>(
        &self,
        poll: &mut crate::supervisor_observation::Journal,
        admission: &mut crate::supervisor_admission::AdmissionJournal,
        run: impl FnOnce(&mut crate::supervisor_admission::AdmissionObservation<'_>) -> R,
    ) -> Result<R, crate::supervisor_admission::ScopeError> {
        use crate::supervisor_admission::{AdmissionObservation, ScopeError};
        // Reject reused evidence before changing either journal or taking custody.
        if poll.bound() { return Err(ScopeError::PollJournalAlreadyBound); }
        if admission.bound() { return Err(ScopeError::AdmissionJournalAlreadyBound); }
        let Some(_lease) = OperationLease::acquire(&self.shared.operation) else {
            return Err(ScopeError::Busy);
        };
        {
            let journal = self.shared.journal.lock()
                .map_err(|_| ScopeError::StartupJournalPoisoned)?;
            if journal.finish_requested { return Err(ScopeError::FinishRequested); }
            if journal.phase != StartPhase::Ready { return Err(ScopeError::StartupNotReady); }
        }
        let owner = {
            let products = self.shared.products.lock()
                .map_err(|_| ScopeError::ProductsPoisoned)?;
            products.owner.clone().ok_or(ScopeError::OwnerMissing)?
        };
        let port = owner.port();
        let Some(supervisor) = self.shared.supervisor.as_ref() else {
            return Err(admission.refuse_scope(&port, ScopeError::SupervisorMissing));
        };
        let mut child = match supervisor.try_lock() {
            Ok(child) => child,
            Err(std::sync::TryLockError::WouldBlock) => {
                return Err(admission.refuse_scope(&port, ScopeError::SupervisorBusy));
            }
            Err(std::sync::TryLockError::Poisoned(_)) => {
                return Err(admission.refuse_scope(&port, ScopeError::SupervisorPoisoned));
            }
        };
        let mut observer = AdmissionObservation::bind(&mut child, poll, admission, &port)?;
        let result = run(&mut observer);
        // Drop executes before releasing the actual child mutex or lease.
        drop(observer);
        // Callback completion alone is not a successful terminal receipt. Keep
        // an impossible/mutated nonterminal or unreturned result fail-closed.
        match admission.latch().and_then(|record| record.returned()) {
            None => Err(ScopeError::UnreturnedLatch),
            Some(crate::mixer_transport::StopReason::None) => Err(ScopeError::AdmissionNotTerminal),
            Some(_) => Ok(result),
        }
    }

    /// Test-fixture cleanup only: preserve poison while allowing the fixture to
    /// signal its own sentinel child and perform an actual wait after an injected
    /// scope panic. This is not a production accessor or operational recovery.
    #[cfg(test)]
    pub fn with_supervisor_for_test<R>(
        &self, run: impl FnOnce(&mut std::process::Child) -> R,
    ) -> R {
        let supervisor = self.shared.supervisor.as_ref().expect("fixture supervisor retained");
        let mut child = supervisor.lock().unwrap_or_else(|error| error.into_inner());
        run(&mut child)
    }
}
