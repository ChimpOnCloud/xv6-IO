// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
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

#include <stdlib.h>
#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct node
{
    struct buf* buffer;//the buffer to be operated;
    int write;//0 is to read, 1 is to write;
    int isfinished;//0 is unfinished, 1 is finished;
    struct node *next;
    struct node *prev;
};

struct nodequeue
{
    struct node *head;
    struct node *tail;
    int size;
}queue = {0, 0, 0};

// void insert(struct m_queue, struct node *m_node)
// {
//     struct node *curr = m_queue.head;
//     while(curr->next != 0)
//     {
//         curr = curr->next;
//     }
//     curr->next = m_node;
//     m_node->prev = curr;
//     m_node->next = 0;
//     m_queue.size++;
// }

struct spinlock bspinlock; //用于调配idequeue的锁

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

//struct nodequeue queue;

void rw()
{
  //调度算法实现区域
  //暂未实现三种算法，采用先来先服务；
  struct node *tmp = queue.head;
  queue.head = queue.head->next;
  virtio_disk_rw(tmp->buffer,tmp->write);
  printf("rw-blocknumber:");
  printf("%d\n", tmp->buffer->blockno);
  tmp->isfinished = 1;
  queue.size--;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) 
  {
    //struct node *tmpnode = (struct node*)malloc(sizeof(struct node));
    struct node m_tmpnode;
    m_tmpnode.buffer = b;
    m_tmpnode.isfinished = 0;
    m_tmpnode.write = 0;
    struct node *tmpnode = &m_tmpnode;
    if(queue.size == 0)
    {
      queue.head = tmpnode;
      queue.tail = tmpnode;
      queue.head->prev = 0;
      queue.tail->next = 0;
    }
    else
    {
      queue.tail->next = tmpnode;
      tmpnode->prev = queue.tail;
      queue.tail = tmpnode;
    }
    queue.size++;
    rw();
    while(!tmpnode->isfinished)
    {

    }
    b->valid = 1;
    printf("actual blockno:");
    printf("%d\n", tmpnode->buffer->blockno);
  }

  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);//write to the disk
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;//move b to the head
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


