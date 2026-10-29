// SPDX-License-Identifier: GPL-3.0-only
//! Inactive Unix two-file persistence with retained in-process evidence.
//!
//! The caller supplies an authenticated ACK and exact target bytes. This module
//! neither derives settings from a frame nor implements SettingsSink. One trusted
//! coordinator must exclude every competing reader/writer, including aliases and
//! temporary names. These checks are NOT inode-CAS or external writer exclusion.
//! A directory descriptor pins an object, not its public pathname. Two renames
//! are NOT an atomic pair. Visibility and fsync completion are separate records.
//! Evidence is bounded process memory, not a crash journal or a recovery permit.
//! Drop does no restoration, retry or temp cleanup. The final writer/evidence
//! handle drop closes the directory. Standard read_to_end/write_all may issue
//! multiple syscalls; no transaction is retried after a returned error.
//! ACLs, xattrs and power-loss guarantees are
//! outside this contract. No ambient paths, sockets, controller or service calls.
use crate::mixer_producer::{BaselineReceipt, FrameAck};
use std::ffi::CString;
use std::fs::{File, Metadata};
use std::io::{Read, Write};
use std::os::fd::{AsRawFd, FromRawFd};
use std::os::unix::fs::MetadataExt;
use std::sync::{Arc, Mutex};

pub const MAX_FILE_BYTES: usize = 65_536;
const NAMES: [&str; 2] = ["knob.ctr", "mixer.state"];
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FileKind {
    Knob,
    Mixer,
}
impl FileKind {
    fn index(self) -> usize {
        match self {
            Self::Knob => 0,
            Self::Mixer => 1,
        }
    }
    fn name(self) -> &'static str {
        NAMES[self.index()]
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairTargets {
    pub knob: Vec<u8>,
    pub mixer: Vec<u8>,
}
impl PairTargets {
    fn get(&self, k: FileKind) -> &[u8] {
        match k {
            FileKind::Knob => &self.knob,
            FileKind::Mixer => &self.mixer,
        }
    }
    fn valid(&self) -> bool {
        self.knob.len() <= MAX_FILE_BYTES && self.mixer.len() <= MAX_FILE_BYTES
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ErrorKind {
    InvalidInput,
    InvalidDirectory,
    InvalidFile,
    UnsupportedOwner,
    TooLarge,
    Io,
    Drift,
    WrongKey,
    OutOfOrder,
    Conflict,
    Closed,
    Overflow,
    Poisoned,
    Interrupted,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairError {
    pub kind: ErrorKind,
    pub operation: &'static str,
    pub errno: Option<i32>,
}
impl PairError {
    fn new(kind: ErrorKind, operation: &'static str) -> Self {
        Self {
            kind,
            operation,
            errno: None,
        }
    }
    fn io(operation: &'static str, e: std::io::Error) -> Self {
        Self {
            kind: ErrorKind::Io,
            operation,
            errno: e.raw_os_error(),
        }
    }
    fn last(operation: &'static str) -> Self {
        Self::io(operation, std::io::Error::last_os_error())
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DirectoryIdentity {
    pub device: u64,
    pub inode: u64,
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
}
impl DirectoryIdentity {
    fn from_metadata(m: &Metadata) -> Self {
        Self {
            device: m.dev(),
            inode: m.ino(),
            mode: m.mode(),
            uid: m.uid(),
            gid: m.gid(),
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileIdentity {
    pub device: u64,
    pub inode: u64,
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
    pub links: u64,
    pub length: u64,
    pub modified_seconds: i64,
    pub modified_nanos: i64,
    pub changed_seconds: i64,
    pub changed_nanos: i64,
}
impl FileIdentity {
    fn from_metadata(m: &Metadata) -> Self {
        Self {
            device: m.dev(),
            inode: m.ino(),
            mode: m.mode(),
            uid: m.uid(),
            gid: m.gid(),
            links: m.nlink(),
            length: m.len(),
            modified_seconds: m.mtime(),
            modified_nanos: m.mtime_nsec(),
            changed_seconds: m.ctime(),
            changed_nanos: m.ctime_nsec(),
        }
    }
    fn same_inode(&self, o: &Self) -> bool {
        self.device == o.device && self.inode == o.inode
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FileImage {
    Absent,
    Present {
        bytes: Vec<u8>,
        identity: FileIdentity,
    },
}
impl FileImage {
    fn identity(&self) -> Option<&FileIdentity> {
        match self {
            Self::Absent => None,
            Self::Present { identity, .. } => Some(identity),
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairSnapshot {
    pub knob: FileImage,
    pub mixer: FileImage,
}
impl PairSnapshot {
    fn get(&self, k: FileKind) -> &FileImage {
        match k {
            FileKind::Knob => &self.knob,
            FileKind::Mixer => &self.mixer,
        }
    }
    fn set(&mut self, k: FileKind, v: FileImage) {
        match k {
            FileKind::Knob => self.knob = v,
            FileKind::Mixer => self.mixer = v,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FileStage {
    Untouched,
    CreateArmed,
    TempCreated,
    WriteArmed,
    Written,
    ModeArmed,
    ModeSet,
    FileSyncArmed,
    FileSynced,
    RenameArmed,
    Renamed,
    DirectorySyncArmed,
    DirectorySynced,
    ReadbackArmed,
    Verified,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileProgress {
    pub stage: FileStage,
    pub temp_name: Option<String>,
    /// Last fd-derived temporary identity, possibly before later write/chmod.
    pub temp_identity: Option<FileIdentity>,
    pub verified: Option<FileImage>,
}
impl Default for FileProgress {
    fn default() -> Self {
        Self {
            stage: FileStage::Untouched,
            temp_name: None,
            temp_identity: None,
            verified: None,
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TransactionResult {
    Pending,
    Committed,
    Failed(PairError),
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Transaction {
    pub ack: FrameAck,
    pub targets: PairTargets,
    pub before: PairSnapshot,
    pub files: [FileProgress; 2],
    pub result: TransactionResult,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CommitReceipt {
    pub ack: FrameAck,
    pub pair: PairSnapshot,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Evidence {
    /// Salvaged poisoned state is diagnostic only.
    pub poisoned: bool,
    pub directory: DirectoryIdentity,
    pub baseline: BaselineReceipt,
    pub original: PairSnapshot,
    /// Last individually verified images: a partial pair is possible.
    pub current: PairSnapshot,
    pub next_request_id: u64,
    pub kernel_sequence: u64,
    pub terminal: Option<PairError>,
    pub last: Option<Transaction>,
}
struct Shared {
    directory: File,
    journal: Mutex<Evidence>,
}
#[derive(Clone)]
pub struct EvidenceHandle {
    shared: Arc<Shared>,
}
impl EvidenceHandle {
    /// No I/O, repair, poison clearing or admission. Retains the same directory.
    pub fn snapshot(&self) -> Evidence {
        match self.shared.journal.lock() {
            Ok(v) => v.clone(),
            Err(e) => {
                let mut v = e.into_inner().clone();
                v.poisoned = true;
                v
            }
        }
    }
}
pub struct SettingsPair {
    shared: Arc<Shared>,
    namespace: String,
    absent_mode: u32,
    #[cfg(test)]
    fault: Option<Box<dyn FnMut(FaultPoint) -> FaultAction + Send>>,
}
#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaultPoint {
    AfterIntent,
    BeforeCreate(FileKind),
    AfterCreate(FileKind),
    BeforeWrite(FileKind),
    AfterWrite(FileKind),
    BeforeMode(FileKind),
    AfterMode(FileKind),
    BeforeFileSync(FileKind),
    AfterFileSync(FileKind),
    BeforeRename(FileKind),
    AfterRename(FileKind),
    BeforeDirectorySync(FileKind),
    AfterDirectorySync(FileKind),
    BeforeReadback(FileKind),
    AfterReadback(FileKind),
    BeforePairVerify,
    AfterPairVerify,
}
#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaultAction {
    Continue,
    Fail(i32),
    Panic,
}
macro_rules! fault {
    ($self:expr,$point:expr) => {{
        #[cfg(test)]
        {
            $self.inject($point)?;
        }
    }};
}

impl SettingsPair {
    pub fn attach(
        directory: File,
        baseline: BaselineReceipt,
        namespace: [u8; 16],
        absent_mode: u32,
    ) -> Result<(Self, EvidenceHandle), PairError> {
        let k = baseline.key;
        if k.instance == [0, 0]
            || k.run_id == 0
            || k.epoch == 0
            || k.producer_id == 0
            || namespace == [0; 16]
            || absent_mode & !0o777 != 0
        {
            return Err(PairError::new(ErrorKind::InvalidInput, "attach scope/mode"));
        }
        if baseline.kernel_sequence == u64::MAX {
            return Err(PairError::new(ErrorKind::Overflow, "attach sequence"));
        }
        let m = directory
            .metadata()
            .map_err(|e| PairError::io("directory metadata", e))?;
        if !m.is_dir() || m.mode() & 0o7000 != 0 {
            return Err(PairError::new(
                ErrorKind::InvalidDirectory,
                "directory type/mode",
            ));
        }
        if m.uid() != unsafe { libc::geteuid() } {
            return Err(PairError::new(
                ErrorKind::UnsupportedOwner,
                "directory owner",
            ));
        }
        let identity = DirectoryIdentity::from_metadata(&m);
        let original = read_pair(&directory)?;
        let namespace = namespace.iter().map(|v| format!("{v:02x}")).collect();
        let shared = Arc::new(Shared {
            directory,
            journal: Mutex::new(Evidence {
                poisoned: false,
                directory: identity,
                baseline,
                original: original.clone(),
                current: original,
                next_request_id: 1,
                kernel_sequence: baseline.kernel_sequence,
                terminal: None,
                last: None,
            }),
        });
        let handle = EvidenceHandle {
            shared: shared.clone(),
        };
        Ok((
            Self {
                shared,
                namespace,
                absent_mode,
                #[cfg(test)]
                fault: None,
            },
            handle,
        ))
    }
    /// One key/generation only. No rebinding or repair. Errors close new writes.
    /// Completed exact duplicates verify current bytes read-only; failed/armed
    /// duplicates return their retained failure, without observing or retrying.
    pub fn commit(
        &mut self,
        ack: FrameAck,
        targets: PairTargets,
    ) -> Result<CommitReceipt, PairError> {
        let s = self.read_journal()?;
        if let Some(last) = &s.last {
            if ack.request.key() == last.ack.request.key()
                && ack.request.request_id() == last.ack.request.request_id()
            {
                if ack != last.ack || targets != last.targets {
                    return self.refuse(PairError::new(ErrorKind::Conflict, "duplicate identity"));
                }
                return match &last.result {
                    TransactionResult::Committed => {
                        if let Err(e) = self.verify_current(&s) {
                            return self.refuse(e);
                        }
                        Ok(CommitReceipt {
                            ack,
                            pair: s.current,
                        })
                    }
                    TransactionResult::Failed(e) => Err(e.clone()),
                    TransactionResult::Pending => self.refuse(PairError::new(
                        ErrorKind::Interrupted,
                        "prior operation has no completion",
                    )),
                };
            }
        }
        if s.terminal.is_some() {
            return Err(PairError::new(ErrorKind::Closed, "terminal component"));
        }
        if s.last
            .as_ref()
            .map(|t| t.result == TransactionResult::Pending)
            .unwrap_or(false)
        {
            return self.refuse(PairError::new(
                ErrorKind::Interrupted,
                "prior operation pending",
            ));
        }
        if !targets.valid() {
            return self.refuse(PairError::new(ErrorKind::TooLarge, "target bytes"));
        }
        if ack.request.key() != s.baseline.key {
            return self.refuse(PairError::new(ErrorKind::WrongKey, "ACK key"));
        }
        let next = match s.kernel_sequence.checked_add(1) {
            Some(n) => n,
            None => return self.refuse(PairError::new(ErrorKind::Overflow, "sequence")),
        };
        if s.next_request_id == u64::MAX {
            return self.refuse(PairError::new(ErrorKind::Overflow, "request ID"));
        }
        if ack.request.request_id() != s.next_request_id || ack.kernel_sequence != next {
            return self.refuse(PairError::new(ErrorKind::OutOfOrder, "ACK order"));
        }
        if let Err(e) = self.verify_current(&s) {
            return self.refuse(e);
        }
        self.update(|e| {
            e.last = Some(Transaction {
                ack,
                targets,
                before: e.current.clone(),
                files: [FileProgress::default(), FileProgress::default()],
                result: TransactionResult::Pending,
            })
        })?;
        match self.perform() {
            Ok(pair) => {
                self.update(|e| {
                    e.current = pair.clone();
                    e.next_request_id = e
                        .next_request_id
                        .checked_add(1)
                        .expect("reserved bounded ID");
                    e.kernel_sequence = ack.kernel_sequence;
                    e.last.as_mut().expect("retained transaction").result =
                        TransactionResult::Committed;
                    if e.kernel_sequence == u64::MAX || e.next_request_id == u64::MAX {
                        e.terminal = Some(PairError::new(
                            ErrorKind::Overflow,
                            "completed final capacity",
                        ));
                    }
                })?;
                Ok(CommitReceipt { ack, pair })
            }
            Err(error) => {
                self.update(|e| {
                    e.last.as_mut().expect("retained transaction").result =
                        TransactionResult::Failed(error.clone());
                    if e.terminal.is_none() {
                        e.terminal = Some(error.clone());
                    }
                })?;
                Err(error)
            }
        }
    }
    fn perform(&mut self) -> Result<PairSnapshot, PairError> {
        fault!(self, FaultPoint::AfterIntent);
        self.replace(FileKind::Knob)?;
        self.replace(FileKind::Mixer)?;
        fault!(self, FaultPoint::BeforePairVerify);
        let e = self.read_journal()?;
        self.verify_current(&e)?;
        fault!(self, FaultPoint::AfterPairVerify);
        Ok(e.current)
    }
    fn replace(&mut self, kind: FileKind) -> Result<(), PairError> {
        let e = self.read_journal()?;
        let t = e.last.as_ref().expect("retained transaction");
        let bytes = t.targets.get(kind).to_vec();
        let source = e.current.get(kind).clone();
        let mode = source
            .identity()
            .map(|i| i.mode & 0o777)
            .unwrap_or(self.absent_mode);
        let name = format!(
            ".settings-pair-{}-{}-{}.new",
            self.namespace,
            t.ack.request.request_id(),
            kind.name()
        );
        self.progress(kind, |p| {
            p.temp_name = Some(name.clone());
            p.stage = FileStage::CreateArmed;
        })?;
        fault!(self, FaultPoint::BeforeCreate(kind));
        self.verify_current(&self.read_journal()?)?;
        let cn = cstring(&name)?;
        let fd = unsafe {
            libc::openat(
                self.shared.directory.as_raw_fd(),
                cn.as_ptr(),
                libc::O_WRONLY | libc::O_CREAT | libc::O_EXCL | libc::O_NOFOLLOW | libc::O_CLOEXEC,
                0o600,
            )
        };
        if fd < 0 {
            return Err(PairError::last("create exclusive temporary"));
        }
        let mut temp = unsafe { File::from_raw_fd(fd) };
        // fd is owned immediately, even if panic prevents identity publication.
        // Neither the created name nor an unowned collision is auto-unlinked.
        fault!(self, FaultPoint::AfterCreate(kind));
        let created = FileIdentity::from_metadata(
            &temp
                .metadata()
                .map_err(|e| PairError::io("temporary metadata", e))?,
        );
        validate_identity(&created)?;
        if let Some(old) = source.identity() {
            if created.uid != old.uid || created.gid != old.gid {
                return Err(PairError::new(
                    ErrorKind::UnsupportedOwner,
                    "temporary owner differs",
                ));
            }
        }
        self.progress(kind, |p| {
            p.temp_identity = Some(created.clone());
            p.stage = FileStage::TempCreated;
        })?;
        self.stage(kind, FileStage::WriteArmed)?;
        fault!(self, FaultPoint::BeforeWrite(kind));
        temp.write_all(&bytes)
            .map_err(|e| PairError::io("write temporary", e))?;
        fault!(self, FaultPoint::AfterWrite(kind));
        self.stage(kind, FileStage::Written)?;
        self.stage(kind, FileStage::ModeArmed)?;
        fault!(self, FaultPoint::BeforeMode(kind));
        if unsafe { libc::fchmod(temp.as_raw_fd(), mode as libc::mode_t) } != 0 {
            return Err(PairError::last("temporary mode"));
        }
        fault!(self, FaultPoint::AfterMode(kind));
        self.stage(kind, FileStage::ModeSet)?;
        self.stage(kind, FileStage::FileSyncArmed)?;
        fault!(self, FaultPoint::BeforeFileSync(kind));
        temp.sync_all()
            .map_err(|e| PairError::io("temporary fsync", e))?;
        fault!(self, FaultPoint::AfterFileSync(kind));
        let ready = FileIdentity::from_metadata(
            &temp
                .metadata()
                .map_err(|e| PairError::io("synced temporary metadata", e))?,
        );
        self.progress(kind, |p| {
            p.temp_identity = Some(ready.clone());
            p.stage = FileStage::FileSynced;
        })?;
        // Observed drift checks, not a conditional rename against other writers.
        self.verify_current(&self.read_journal()?)?;
        let candidate = read_name(&self.shared.directory, &name)?;
        if !target_matches(&candidate, &ready, &bytes, mode) || !ready.same_inode(&created) {
            return Err(PairError::new(
                ErrorKind::Drift,
                "temporary changed before rename",
            ));
        }
        self.stage(kind, FileStage::RenameArmed)?;
        fault!(self, FaultPoint::BeforeRename(kind));
        self.verify_current(&self.read_journal()?)?;
        if !target_matches(
            &read_name(&self.shared.directory, &name)?,
            &ready,
            &bytes,
            mode,
        ) {
            return Err(PairError::new(
                ErrorKind::Drift,
                "temporary changed at rename boundary",
            ));
        }
        let target = cstring(kind.name())?;
        if unsafe {
            libc::renameat(
                self.shared.directory.as_raw_fd(),
                cn.as_ptr(),
                self.shared.directory.as_raw_fd(),
                target.as_ptr(),
            )
        } != 0
        {
            return Err(PairError::last("rename temporary"));
        }
        fault!(self, FaultPoint::AfterRename(kind));
        self.stage(kind, FileStage::Renamed)?;
        self.stage(kind, FileStage::DirectorySyncArmed)?;
        fault!(self, FaultPoint::BeforeDirectorySync(kind));
        self.shared
            .directory
            .sync_all()
            .map_err(|e| PairError::io("directory fsync", e))?;
        fault!(self, FaultPoint::AfterDirectorySync(kind));
        self.stage(kind, FileStage::DirectorySynced)?;
        self.stage(kind, FileStage::ReadbackArmed)?;
        fault!(self, FaultPoint::BeforeReadback(kind));
        self.verify_directory()?;
        let observed = read_name(&self.shared.directory, kind.name())?;
        if !target_matches(&observed, &ready, &bytes, mode) {
            return Err(PairError::new(
                ErrorKind::Drift,
                "replacement readback differs",
            ));
        }
        fault!(self, FaultPoint::AfterReadback(kind));
        self.update(|e| {
            let p = &mut e.last.as_mut().expect("retained transaction").files[kind.index()];
            p.stage = FileStage::Verified;
            p.verified = Some(observed.clone());
            e.current.set(kind, observed);
        })?;
        Ok(())
    }
    fn verify_directory(&self) -> Result<(), PairError> {
        let current = self
            .shared
            .directory
            .metadata()
            .map_err(|e| PairError::io("directory recheck", e))?;
        if DirectoryIdentity::from_metadata(&current) != self.read_journal()?.directory {
            return Err(PairError::new(
                ErrorKind::Drift,
                "retained directory changed",
            ));
        }
        Ok(())
    }
    fn verify_current(&self, e: &Evidence) -> Result<(), PairError> {
        self.verify_directory()?;
        if read_pair(&self.shared.directory)? != e.current {
            return Err(PairError::new(ErrorKind::Drift, "current pair changed"));
        }
        Ok(())
    }
    fn read_journal(&self) -> Result<Evidence, PairError> {
        self.shared
            .journal
            .lock()
            .map(|e| e.clone())
            .map_err(|_| PairError::new(ErrorKind::Poisoned, "journal poisoned"))
    }
    fn update(&self, f: impl FnOnce(&mut Evidence)) -> Result<(), PairError> {
        let mut e = self
            .shared
            .journal
            .lock()
            .map_err(|_| PairError::new(ErrorKind::Poisoned, "journal poisoned"))?;
        f(&mut e);
        Ok(())
    }
    fn progress(&self, k: FileKind, f: impl FnOnce(&mut FileProgress)) -> Result<(), PairError> {
        self.update(|e| f(&mut e.last.as_mut().expect("retained transaction").files[k.index()]))
    }
    fn stage(&self, k: FileKind, s: FileStage) -> Result<(), PairError> {
        self.progress(k, |p| p.stage = s)
    }
    fn refuse<T>(&self, error: PairError) -> Result<T, PairError> {
        self.update(|e| {
            if e.terminal.is_none() {
                e.terminal = Some(error.clone());
            }
        })?;
        Err(error)
    }
    #[cfg(test)]
    pub fn set_fault_hook(&mut self, hook: Box<dyn FnMut(FaultPoint) -> FaultAction + Send>) {
        self.fault = Some(hook);
    }
    #[cfg(test)]
    fn inject(&mut self, p: FaultPoint) -> Result<(), PairError> {
        match self
            .fault
            .as_mut()
            .map(|f| f(p))
            .unwrap_or(FaultAction::Continue)
        {
            FaultAction::Continue => Ok(()),
            FaultAction::Fail(errno) => Err(PairError {
                kind: ErrorKind::Io,
                operation: "injected boundary failure",
                errno: Some(errno),
            }),
            FaultAction::Panic => panic!("injected settings-pair boundary panic: {p:?}"),
        }
    }
    #[cfg(test)]
    pub fn poison_journal_for_test(&self) {
        let _held = self.shared.journal.lock().unwrap();
        panic!("intentional diagnostic journal poison");
    }
}
fn cstring(name: &str) -> Result<CString, PairError> {
    CString::new(name).map_err(|_| PairError::new(ErrorKind::InvalidInput, "embedded name NUL"))
}
fn validate_identity(i: &FileIdentity) -> Result<(), PairError> {
    if i.mode & libc::S_IFMT as u32 != libc::S_IFREG as u32 || i.links != 1 || i.mode & 0o7000 != 0
    {
        return Err(PairError::new(
            ErrorKind::InvalidFile,
            "regular single-link ordinary-mode file required",
        ));
    }
    if i.uid != unsafe { libc::geteuid() } || i.gid != unsafe { libc::getegid() } {
        return Err(PairError::new(
            ErrorKind::UnsupportedOwner,
            "file owner/group",
        ));
    }
    if i.length > MAX_FILE_BYTES as u64 {
        return Err(PairError::new(ErrorKind::TooLarge, "file size"));
    }
    Ok(())
}
fn read_pair(directory: &File) -> Result<PairSnapshot, PairError> {
    Ok(PairSnapshot {
        knob: read_name(directory, NAMES[0])?,
        mixer: read_name(directory, NAMES[1])?,
    })
}
fn read_name(directory: &File, name: &str) -> Result<FileImage, PairError> {
    let name = cstring(name)?;
    let mut before: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe {
        libc::fstatat(
            directory.as_raw_fd(),
            name.as_ptr(),
            &mut before,
            libc::AT_SYMLINK_NOFOLLOW,
        )
    } != 0
    {
        let e = std::io::Error::last_os_error();
        if e.raw_os_error() == Some(libc::ENOENT) {
            return Ok(FileImage::Absent);
        }
        return Err(PairError::io("name metadata", e));
    }
    if (before.st_mode as u32) & libc::S_IFMT as u32 != libc::S_IFREG as u32
        || before.st_nlink != 1
        || (before.st_mode as u32) & 0o7000 != 0
    {
        return Err(PairError::new(
            ErrorKind::InvalidFile,
            "name is not an ordinary single-link regular file",
        ));
    }
    let fd = unsafe {
        libc::openat(
            directory.as_raw_fd(),
            name.as_ptr(),
            libc::O_RDONLY | libc::O_NONBLOCK | libc::O_NOFOLLOW | libc::O_CLOEXEC,
        )
    };
    if fd < 0 {
        return Err(PairError::last("open regular file"));
    }
    let mut file = unsafe { File::from_raw_fd(fd) };
    let initial = FileIdentity::from_metadata(
        &file
            .metadata()
            .map_err(|e| PairError::io("opened metadata", e))?,
    );
    validate_identity(&initial)?;
    if initial.device != before.st_dev as u64
        || initial.inode != before.st_ino as u64
        || initial.mode != before.st_mode as u32
        || initial.links != before.st_nlink as u64
        || initial.uid != before.st_uid
        || initial.gid != before.st_gid
        || initial.length != before.st_size as u64
    {
        return Err(PairError::new(
            ErrorKind::Drift,
            "name changed while opening",
        ));
    }
    let mut bytes = Vec::new();
    (&mut file)
        .take(MAX_FILE_BYTES as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| PairError::io("read regular file", e))?;
    let end = FileIdentity::from_metadata(
        &file
            .metadata()
            .map_err(|e| PairError::io("read-end metadata", e))?,
    );
    let mut after: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe {
        libc::fstatat(
            directory.as_raw_fd(),
            name.as_ptr(),
            &mut after,
            libc::AT_SYMLINK_NOFOLLOW,
        )
    } != 0
    {
        return Err(PairError::last("read-end name metadata"));
    }
    let after_name = FileIdentity::from_metadata(
        &file
            .metadata()
            .map_err(|e| PairError::io("final fd metadata", e))?,
    );
    if initial != end
        || initial != after_name
        || bytes.len() as u64 != initial.length
        || after.st_dev as u64 != initial.device
        || after.st_ino as u64 != initial.inode
        || after.st_mode as u32 != initial.mode
        || after.st_nlink as u64 != initial.links
        || after.st_uid != initial.uid
        || after.st_gid != initial.gid
        || after.st_size as u64 != initial.length
    {
        return Err(PairError::new(ErrorKind::Drift, "file changed during read"));
    }
    Ok(FileImage::Present {
        bytes,
        identity: initial,
    })
}
fn target_matches(image: &FileImage, identity: &FileIdentity, bytes: &[u8], mode: u32) -> bool {
    match image {
        FileImage::Present {
            bytes: seen,
            identity: actual,
        } => {
            seen == bytes
                && actual.same_inode(identity)
                && actual.mode & 0o777 == mode
                && actual.uid == identity.uid
                && actual.gid == identity.gid
                && actual.links == 1
        }
        FileImage::Absent => false,
    }
}

#[cfg(test)]
#[path = "settings_pair_tests.rs"]
mod tests;
