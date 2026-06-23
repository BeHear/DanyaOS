/// Inter-Process Communication — message queue
/// Migrated from src/process/ipc.c
/// Uses lock-free circular buffer with atomic indices.

use core::sync::atomic::{AtomicU32, Ordering};

const IPC_MSG_SIZE: usize = 256;
const IPC_MAX_MSGS: usize = 64;

#[repr(C)]
pub struct IpcMessage {
    pub sender: i32,
    pub receiver: i32,
    pub data: [u8; IPC_MSG_SIZE],
    pub length: u32,
    pub used: bool,
}

impl IpcMessage {
    const fn new() -> Self {
        Self {
            sender: 0,
            receiver: 0,
            data: [0u8; IPC_MSG_SIZE],
            length: 0,
            used: false,
        }
    }
}

static mut MESSAGE_QUEUE: [IpcMessage; IPC_MAX_MSGS] = {
    // Const initialization: create array of zeroed messages
    const INIT: IpcMessage = IpcMessage::new();
    [INIT; IPC_MAX_MSGS]
};
static QUEUE_HEAD: AtomicU32 = AtomicU32::new(0);
static QUEUE_TAIL: AtomicU32 = AtomicU32::new(0);

/// Initialize the IPC message queue.
///
/// # Safety
/// Must be called once during boot. Single-threaded init only.
#[no_mangle]
pub unsafe extern "C" fn rust_ipc_init() {
    let ptr = &raw mut MESSAGE_QUEUE as *mut IpcMessage;
    for i in 0..IPC_MAX_MSGS {
        let msg = &mut *ptr.add(i);
        msg.used = false;
        msg.length = 0;
    }
    QUEUE_HEAD.store(0, Ordering::Relaxed);
    QUEUE_TAIL.store(0, Ordering::Relaxed);
}

/// Send a message to a process.
/// Returns length sent on success, -1 on failure (queue full or invalid receiver).
///
/// # Safety
/// `data` must point to a valid buffer of at least `length` bytes.
#[no_mangle]
pub unsafe extern "C" fn rust_ipc_send(
    to: i32,
    data: *const u8,
    length: u32,
) -> i32 {
    let len = if length > IPC_MSG_SIZE as u32 {
        IPC_MSG_SIZE as u32
    } else {
        length
    };

    let tail = QUEUE_TAIL.load(Ordering::Relaxed);
    let next = (tail + 1) % IPC_MAX_MSGS as u32;
    let head = QUEUE_HEAD.load(Ordering::Acquire);

    if next == head {
        // Queue full
        return -1;
    }

    // Validate receiver exists (pid must be > 0 and <= MAX_PROCESSES=32)
    if to < 1 || to > 32 {
        return -1;
    }

    let msg = &mut MESSAGE_QUEUE[tail as usize];
    msg.sender = 0; // Will be set by caller if needed
    msg.receiver = to;

    // Safe copy: len is clamped to IPC_MSG_SIZE
    let src_slice = core::slice::from_raw_parts(data, len as usize);
    let dst_slice = core::slice::from_raw_parts_mut(msg.data.as_mut_ptr(), len as usize);
    dst_slice.copy_from_slice(src_slice);

    msg.length = len;
    msg.used = true;

    QUEUE_TAIL.store(next, Ordering::Release);
    len as i32
}

/// Receive a message. If `from` is 0, receives from any sender.
/// Returns length received, 0 if no messages available.
///
/// # Safety
/// `buf` must point to a valid buffer of at least `max_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn rust_ipc_receive(
    from: i32,
    buf: *mut u8,
    max_len: u32,
) -> i32 {
    let head = QUEUE_HEAD.load(Ordering::Relaxed);
    let tail = QUEUE_TAIL.load(Ordering::Acquire);

    if head == tail {
        return 0;
    }

    let msg = &mut MESSAGE_QUEUE[head as usize];
    if !msg.used {
        QUEUE_HEAD.store((head + 1) % IPC_MAX_MSGS as u32, Ordering::Release);
        return 0;
    }

    if from != 0 && msg.sender != from {
        return 0;
    }

    let copy_len = if msg.length > max_len {
        max_len
    } else {
        msg.length
    };

    let src_slice = core::slice::from_raw_parts(msg.data.as_ptr(), copy_len as usize);
    let dst_slice = core::slice::from_raw_parts_mut(buf, copy_len as usize);
    dst_slice.copy_from_slice(src_slice);

    msg.used = false;
    let new_head = (head + 1) % IPC_MAX_MSGS as u32;
    QUEUE_HEAD.store(new_head, Ordering::Release);
    copy_len as i32
}
