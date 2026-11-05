// Buffer cache.
//
// The buffer cache is a set of hash buckets of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

// Unused buffers use this device number so they cannot match a real block.
#define DEVNONE ((uint)-1)

struct {
  struct spinlock lock[NBUCKETS];
  struct spinlock alloc_lock;
  struct buf buf[NBUF];

  // One doubly-linked circular list per hash bucket.
  struct buf head[NBUCKETS];
} bcache;

static char *bucket_names[NBUCKETS] = {
  "bcache0", "bcache1", "bcache2", "bcache3", "bcache4",
  "bcache5", "bcache6", "bcache7", "bcache8", "bcache9",
  "bcache10", "bcache11", "bcache12"
};

static int
hash(uint blockno)
{
  return blockno % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;
  int i;

  initlock(&bcache.alloc_lock, "bcache-alloc");

  for(i = 0; i < NBUCKETS; i++){
    initlock(&bcache.lock[i], bucket_names[i]);
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  for(i = 0, b = bcache.buf; b < bcache.buf+NBUF; b++, i++){
    int bi = i % NBUCKETS;

    b->next = bcache.head[bi].next;
    b->prev = &bcache.head[bi];
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->valid = 0;
    b->dev = DEVNONE;
    b->blockno = 0;
    bcache.head[bi].next->prev = b;
    bcache.head[bi].next = b;
  }
}

// Caller must hold bcache.lock[bi].
static struct buf*
bfind(uint dev, uint blockno, int bi)
{
  struct buf *b;

  for(b = bcache.head[bi].next; b != &bcache.head[bi]; b = b->next){
    if(b->dev == dev && b->blockno == blockno)
      return b;
  }
  return 0;
}

// Caller must hold bcache.lock[bi].
static struct buf*
bfree(int bi)
{
  struct buf *b;

  for(b = bcache.head[bi].next; b != &bcache.head[bi]; b = b->next){
    if(b->refcnt == 0)
      return b;
  }
  return 0;
}

// Move b from its current list onto bucket bi.
// Caller must hold the locks of both buckets (or just bi if b is already there).
static void
bmove(struct buf *b, int bi)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
  b->next = bcache.head[bi].next;
  b->prev = &bcache.head[bi];
  bcache.head[bi].next->prev = b;
  bcache.head[bi].next = b;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bi = hash(blockno);
  int i;

  acquire(&bcache.lock[bi]);
  b = bfind(dev, blockno, bi);
  if(b){
    b->refcnt++;
    release(&bcache.lock[bi]);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.lock[bi]);

  // Slow path: serialize allocation, then recheck the target bucket.
  // Lock order: alloc_lock -> target bucket -> donor bucket.
  acquire(&bcache.alloc_lock);
  acquire(&bcache.lock[bi]);
  b = bfind(dev, blockno, bi);
  if(b){
    b->refcnt++;
    release(&bcache.lock[bi]);
    release(&bcache.alloc_lock);
    acquiresleep(&b->lock);
    return b;
  }

  b = bfree(bi);
  if(b == 0){
    for(i = 0; i < NBUCKETS; i++){
      if(i == bi)
        continue;
      acquire(&bcache.lock[i]);
      b = bfree(i);
      if(b){
        bmove(b, bi);
        release(&bcache.lock[i]);
        break;
      }
      release(&bcache.lock[i]);
    }
  }

  if(b == 0)
    panic("bget: no buffers");

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcache.lock[bi]);
  release(&bcache.alloc_lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  int bi;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // A live reference keeps this buf from being recycled, so its
  // bucket identity is stable until refcnt is decremented.
  bi = hash(b->blockno);
  acquire(&bcache.lock[bi]);
  b->refcnt--;
  release(&bcache.lock[bi]);
}

void
bpin(struct buf *b) {
  int bi = hash(b->blockno);

  acquire(&bcache.lock[bi]);
  b->refcnt++;
  release(&bcache.lock[bi]);
}

void
bunpin(struct buf *b) {
  int bi = hash(b->blockno);

  acquire(&bcache.lock[bi]);
  b->refcnt--;
  release(&bcache.lock[bi]);
}
