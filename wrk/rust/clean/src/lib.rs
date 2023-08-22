//Crossbeam should, but does not make this faster.
//use crossbeam::channel::bounded;
use std::{net::TcpStream, sync::mpsc, thread};
type Job = (fn(TcpStream), TcpStream);

type Sender = mpsc::Sender<Job>;
//type Sender = crossbeam::channel::Sender<Job>;

type Receiver = mpsc::Receiver<Job>;
//type Receiver = crossbeam::channel::Receiver<Job>;

pub struct ThreadPool {
    workers: Vec<Worker>,
    senders: Vec<Sender>,

    next_sender: usize,
}

impl ThreadPool {
    /// Create a new ThreadPool.
    ///
    /// The size is the number of threads in the pool.
    ///
    /// # Panics
    ///
    /// The `new` function will panic if the size is zero.
    pub fn new(size: usize) -> ThreadPool {
        assert!(size > 0);

        let mut workers = Vec::with_capacity(size);
        let mut senders = Vec::with_capacity(size);

        for id in 0..size {
            //let (sender, receiver) = bounded(2);
            let (sender, receiver) = mpsc::channel();
            senders.push(sender);
            workers.push(Worker::new(id, receiver));
        }

        ThreadPool {
            workers,
            senders,
            next_sender: 0,
        }
    }
    /// round robin over available workers to ensure we never have to buffer requests
    pub fn execute(&mut self, handler: fn(TcpStream), stream: TcpStream) {
        let job = (handler, stream);
        self.senders[self.next_sender].send(job).unwrap();
        //self.senders[self.next_sender].try_send(job).unwrap();
        self.next_sender += 1;
        if self.next_sender == self.senders.len() {
            self.next_sender = 0;
        }
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        self.senders.clear();

        for worker in &mut self.workers {
            println!("Shutting down worker {}", worker.id);
            if let Some(thread) = worker.thread.take() {
                thread.join().unwrap();
            }
        }
    }
}

struct Worker {
    id: usize,
    thread: Option<thread::JoinHandle<()>>,
}

impl Worker {
    fn new(id: usize, receiver: Receiver) -> Worker {
        let thread = thread::spawn(move || Self::work(receiver));

        Worker {
            id,
            thread: Some(thread),
        }
    }

    fn work(receiver: Receiver) {
        loop {
            let message = receiver.recv();
            match message {
                Ok((handler, stream)) => {
                    // println!("Worker  got a job; executing.");
                    handler(stream);
                }
                Err(_) => {
                    // println!("Worker  disconnected; shutting down.");
                    break;
                }
            }
        }
    }
}
