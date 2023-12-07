# user-land-filesystem
The repository is mainly for course project, aiming at file system teaching process.

基于FUSE实现的EXT2文件系统。完成了必做功能和部分选做功能，具体完成情况如下表所示：

| 必做功能           | 完成情况 |
| ------------------ | ---- |
| 挂载文件系统       |   已完成   |
| 卸载文件系统       | 已完成 |
| 创建文件/文件夹    | 已完成 |
| 查看文件夹下的文件 | 已完成 |

| 选做功能               | 完成情况                               |
| ---------------------- | -------------------------------------- |
| 间接索引和二级间接索引 | 未实现                                 |
| 删除操作               | 已完成，包括-r                         |
| 文件移动               | 已完成                                 |
| 读写文件               | 可用cat/echo/tee读写，vim/vscode未实现 |
| 软硬连接               | 未实现                                 |

# Background

## FUSE

FUSE框架的简单原理大致为：FUSE-Kernel会拦截Kernel中的VFS子模块接收到的来自Userspace的读写文件请求，通过某些方式调用Userspace实现的钩子函数来处理这些请求，具体的文件系统的meta data在用户态管理。我们本次实验要做的，就是在FUSE框架提供的接口的基础上，实现FUSE所要求的相关钩子函数，从而实现一个青春版的EXT2文件系统。

因而，首先就是明确要实现哪些钩子函数：

```c
static struct fuse_operations operations = {
    .init = newfs_init,                      /* mount文件系统 */        
    .destroy = newfs_destroy,                /* umount文件系统 */
    .mkdir = newfs_mkdir,                    /* 建目录，mkdir */
    .getattr = newfs_getattr,                /* 获取文件属性，类似stat，必须完成 */
    .readdir = newfs_readdir,                /* 填充dentrys */
    .mknod = newfs_mknod,                    /* 创建文件，touch相关 */
    .write = newfs_write,                                    /* 写入文件 */
    .read = newfs_read,                                  /* 读文件 */
    .utimens = newfs_utimens,                /* 修改时间，忽略，避免touch报错 */
    .truncate = newfs_truncate,                              /* 改变文件大小 */
    .unlink = newfs_unlink,                                  /* 删除文件 */
    .rmdir  = newfs_rmdir,                                   /* 删除目录， rm -r */
    .rename = newfs_rename,                                  /* 重命名，mv */

    .open = newfs_open,                         
    .opendir = newfs_opendir,
    .access = newfs_access
};
```

可以看到，这些钩子函数涵盖了实验要求所需的挂载卸载、创建删除文件和目录、获取文件属性、读写文件等功能。我们只需实现这些钩子函数，就能在FUSE的基础上实现一个简单的文件系统。

# 问题与收获

## 问题

一个小bug与对FUSE的实现的浅层理解

在我的数据结构设计中，inode的size字段是以逻辑块为单位的。也即，若inode->size = 1，表明对应的文件占据一个逻辑块大小，也即1024字节。这点是与示例的sfs不一样的。于是，在参照sfs的过程中，我在这个地方：

```c
// 正确为: newfs_stat->st_size = NFS_BLKS_SZ(dentry->inode->size);
newfs_stat->st_size = dentry->inode->size;
```

不小心写错了，导致在实现文件读写的时候，虽然磁盘里的内容和debug输出都正确，但实际结果就错误了，只输出了一个字符。可见，用户态文件系统还是先通过getattr得到属性，从而得到文件大小，从而进行文件读写的。

感觉可以从此窥见用户态文件系统的大体思路，大概就是通过这个getattr获取到文件大小、可读可写等meta data（这也很显然，毕竟inode什么的存在userspace，kernel也不能直接获取），然后在内核中可能大概遵循这么个代码框架：

（下面只是伪代码简要表示，kernel肯定不能直接调用userspace的函数，大概是凭借eBPF或FUSE框架实现的系统调用）

```c
int read() {
    int size = fuse->getattr(path, &stat); // 调用钩子函数
    int io_size = 512;
    int offset = 0;
    while(offset < size) {
        offset += fuse->read(path, buf, io_size, offset);
    }
}
```

然后fuse的read实现函数再通过linux实现的磁盘驱动跟底层硬件交互，就像本次实验的ddriver虚拟磁盘驱动，形成一个kernel -> userspace -> kernel -> hardware的结构。

更进一步的，其余的类似将kernel中的功能搬进用户态实现的东西，比如说用户态调度框架，我感觉事实上都是遵循着这样的思路，也即内核提供钩子函数、用户态通过eBPF or 系统调用注入实现钩子函数，从而成功将用户态代码注入内核。

## 收获

感觉实验设计得很好，很巧妙将对文件系统的考查聚焦在了FUSE（用户态文件系统）上。相比于xv6对文件系统的考查（软链接、mmap），本实验的考查更加宏观，更注重展现文件系统的基本全貌。但同时，由于它基于用户态的FUSE，故而又避免了对相较笨重的Linux进行修改，同时也体现了实验的与时俱进（相较于本部基于linux0.11的文件系统实验）。

通过这次实验，我加深了对文件系统的理解。首先，要说文件系统是什么，离不开文件系统的布局。它就是个super block + 位图（索引块and数据块） + 索引块 + 数据块。它通过这些meta data，来将扁平化的磁盘存储，转化为一个层次化的文件树结构。我们每次使用操作系统，都是先读出根目录的数据块（固定为第0块），然后就能知道它里面的索引项，从而能读出更多目录和文件了。这个发现很新奇，因为以前在了解操作系统初始化过程的时候，都难以想象文件系统是怎么初始化的，现在算是明白了一些。

然后还有一点让人印象深刻，就是实际上是挂载的时候从磁盘读出文件系统相关信息（super、位图），解析文件系统的层次结构；然后，我们在卸载的时候再将文件系统的meta data和文件数据写回磁盘（简单实现）。以前一直不大明白挂载和卸载的原理，这点也让我恍然大悟。

还有就是简单了解了下各类文件系统的差别，如FAT32和EXT2之间的差别，前者感觉就是以前的计算机导论课上学到的方式，EXT这些才是带有层次结构、inode这些概念的较现代化的文件系统。
