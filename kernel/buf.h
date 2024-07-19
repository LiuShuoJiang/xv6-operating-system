struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  // ===================== Lab8: Buffer Cache =====================:
  // commented out for Lab8
  // struct buf *prev; // LRU cache list
  // :===================== Lab8: Buffer Cache =====================
  struct buf *next;
  uchar data[BSIZE];

  // ===================== Lab8: Buffer Cache =====================:
  // trash: this buf contains invalid dev and blockno
  // and needs to be evicted and re-hashed before use.
  int trash;
  uint lastUseTimestamp;
  // :===================== Lab8: Buffer Cache =====================
};

