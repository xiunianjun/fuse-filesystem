extern struct newfs_super      newfs_super; 
extern struct custom_options newfs_options;
#include "newfs.h"

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 分配一个数据块，占用位图
 * @return 数据块的offset
 */
int newfs_alloc_data_blk() {
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int data_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(newfs_super.data_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            data_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || data_cursor == newfs_super.data_blks)  // TODO
        return -NFS_ERROR_NOSPACE;

    return data_cursor;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(newfs_super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == (INODE_PER_BLK * newfs_super.ino_blks))  // TODO
        return NULL;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    memset(inode->block_pointer, 0, sizeof(int) * NFS_MAX_SIZE_PER_FILE);
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    // TODO, 动态分配，所以一开始先一个块也不分配
    // if (NFS_IS_REG(inode)) {    
        // inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(1));
    // }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.block_pointer, inode->block_pointer, sizeof(int) * NFS_MAX_SIZE_PER_FILE);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset = 0;
    int write_length = 0;
    int index = 0;
    
                                                      /* Cycle 1: 写 INODE */
    if (newfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }
                                                      /* Cycle 2: 写 数据 */
    dentry_cursor = inode->dentrys;
    if (NFS_IS_DIR(inode) && dentry_cursor != NULL) {   
        offset = NFS_DATA_OFS(inode->block_pointer[index ++]);
        while (dentry_cursor != NULL) {
            assert(index < NFS_MAX_SIZE_PER_FILE);
            memcpy(dentry_d.fname, dentry_cursor->name, NFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;                     
            }
            
            if (dentry_cursor->inode != NULL) {
                newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
            write_length += sizeof(struct newfs_dentry_d);
            if (write_length + sizeof(struct newfs_dentry_d) > NFS_LOGIC_SZ()) {
                write_length = 0;
                offset = NFS_DATA_OFS(inode->block_pointer[index ++]);
            }
        }
    }
    else if (NFS_IS_REG(inode)) {
        for (int i = 0; i < inode->size; i ++) {
            if (newfs_driver_write(NFS_DATA_OFS(inode->block_pointer[i]), inode->data + i * NFS_LOGIC_SZ(), 
                                NFS_LOGIC_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }
    }
    return NFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    printf("~ hello ~\n");
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0;
    int read_length = 0;
    int offset = 0;
    int index = 0;

    if (newfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->block_pointer, inode_d.block_pointer, NFS_MAX_SIZE_PER_FILE * sizeof(int));
    inode->dentry = dentry;
    inode->dentrys = NULL;
    if (NFS_IS_DIR(inode)) {
        offset = NFS_DATA_OFS(inode->block_pointer[index ++]);
        dir_cnt = inode_d.dir_cnt;
        for (int i = 0; i < dir_cnt; i ++) {
            if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);
            read_length += sizeof(struct newfs_dentry_d);
            offset += sizeof(struct newfs_dentry_d);
            if (read_length + sizeof(struct newfs_dentry_d) > NFS_LOGIC_SZ()) {
                read_length = 0;
                offset = NFS_DATA_OFS(inode->block_pointer[index ++]);
            }
        }
    }
    else if (NFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(inode->size));
        for (int i = 0; i < inode->size; i ++) {
            if (newfs_driver_read(NFS_DATA_OFS(inode->block_pointer[i]), (uint8_t *)(inode->data + i * NFS_LOGIC_SZ()), 
                                NFS_LOGIC_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }   
        }
    }
    return inode;
}

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != 0) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 
 * 解析路径，返回文件对应的上级目录。
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct newfs_inode* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;   // 一层一层地获取每级目录的inode

        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   // 遍历所有目录项
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

void print_statistics() {
    printf("==================================================\n");
    printf("==================================================\n");
    printf("sb_offset : %d\n", newfs_super.sb_offset);      /* 建立 in-memory 结构 */
    printf("sb_blks : %d\n", newfs_super.sb_blks);
    printf("ino_map_offset : %d\n", newfs_super.ino_map_offset);
    printf("ino_map_blks : %d\n", newfs_super.ino_map_blks);
    printf("data_map_offset : %d\n", newfs_super.data_map_offset);
    printf("data_map_blks : %d\n", newfs_super.data_map_blks);
    printf("ino_offset : %d\n", newfs_super.ino_offset);
    printf("ino_blks : %d\n", newfs_super.ino_blks);
    printf("data_offset : %d\n", newfs_super.data_offset);
    printf("data_blks : %d\n", newfs_super.data_blks);
    printf("sz_usage    : %d\n", newfs_super.sz_usage   );
    printf("==================================================\n");
    printf("==================================================\n");
}

/**
 * @brief 挂载newfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inodes | Data |
 * 
 * 2 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  newfs_super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;

    int                 logic_blk_num;
    int                 map_inode_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    newfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &newfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    // Set one logic block = 2 IO block
    newfs_super.sz_logic = newfs_super.sz_io * 2;
    
    root_dentry = new_dentry("/", NFS_DIR);

    // 读入超级块
    if (newfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   

                                                      /* 读取super */
    if (newfs_super_d.magic_num != NFS_MAGIC_NUM) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
                                                      // 这个应该是初次挂载吧，然后之后umount再卸载写回
        // 逻辑块总数
        logic_blk_num  =  NFS_DISK_SZ() / NFS_LOGIC_SZ();
                                                   /* 布局layout */
        // 注意，单位都是逻辑块
        newfs_super_d.sb_offset = 0;
        newfs_super_d.sb_blks = 1;
        newfs_super_d.ino_map_offset = newfs_super_d.sb_offset + newfs_super_d.sb_blks;
        newfs_super_d.ino_map_blks = 1;
        newfs_super_d.data_map_offset = newfs_super_d.ino_map_offset + newfs_super_d.ino_map_blks;
        newfs_super_d.data_map_blks = 1;
        newfs_super_d.ino_offset = newfs_super_d.data_map_offset + newfs_super_d.data_map_blks;
        newfs_super_d.ino_blks = NFS_ROUND_UP((logic_blk_num * sizeof(struct newfs_inode_d)), NFS_LOGIC_SZ()) / NFS_LOGIC_SZ();
        newfs_super_d.data_offset = newfs_super_d.ino_offset + newfs_super_d.ino_blks;
        newfs_super_d.data_blks = logic_blk_num - newfs_super_d.sb_blks - newfs_super_d.ino_map_blks - newfs_super_d.data_map_blks - newfs_super_d.ino_blks;

        newfs_super_d.sz_usage    = 0;
        is_init = TRUE;
    }
    newfs_super.sb_offset = newfs_super_d.sb_offset;      /* 建立 in-memory 结构 */
    newfs_super.sb_blks = newfs_super_d.sb_blks;
    newfs_super.ino_map_offset = newfs_super_d.ino_map_offset;
    newfs_super.ino_map_blks = newfs_super_d.ino_map_blks;
    newfs_super.data_map_offset = newfs_super_d.data_map_offset;
    newfs_super.data_map_blks = newfs_super_d.data_map_blks;
    newfs_super.ino_offset = newfs_super_d.ino_offset;
    newfs_super.ino_blks = newfs_super_d.ino_blks;
    newfs_super.data_offset = newfs_super_d.data_offset;
    newfs_super.data_blks = newfs_super_d.data_blks;
    newfs_super.sz_usage    = newfs_super_d.sz_usage;
    print_statistics();

    // TODO: if init, should zero?
    newfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(newfs_super.ino_map_blks));
    newfs_super.map_data = (uint8_t*)malloc(NFS_BLKS_SZ(newfs_super.data_map_blks));

    if (!is_init) {
        if (newfs_driver_read(NFS_BLKS_SZ(newfs_super_d.ino_map_offset), (uint8_t *)(newfs_super.map_inode), 
                            NFS_BLKS_SZ(newfs_super_d.ino_map_blks)) != NFS_ERROR_NONE) {
            return -NFS_ERROR_IO;
        }

        if (newfs_driver_read(NFS_BLKS_SZ(newfs_super_d.data_map_offset), (uint8_t *)(newfs_super.map_data), 
                            NFS_BLKS_SZ(newfs_super_d.data_map_blks)) != NFS_ERROR_NONE) {
            return -NFS_ERROR_IO;
        }
    } else {
        memset(newfs_super.map_inode, 0, NFS_BLKS_SZ(newfs_super.ino_map_blks));
        memset(newfs_super.map_data, 0, NFS_BLKS_SZ(newfs_super.data_map_blks));
    }

    // TODO 根节点的建立与分配
    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;

    printf("===================  inode map  ===================\n");
    // newfs_dump_inode_map();
    printf("===================   data map  ===================\n");
    // newfs_dump_data_map();
    return ret;
}

/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    // TODO 刷回所有数据、inode
    newfs_sync_inode(newfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */

    newfs_super_d.magic_num           = NFS_MAGIC_NUM;
    newfs_super_d.sb_offset = newfs_super.sb_offset;      /* 建立 in-disk 结构 */
    newfs_super_d.sb_blks = newfs_super.sb_blks;
    newfs_super_d.ino_map_offset = newfs_super.ino_map_offset;
    newfs_super_d.ino_map_blks = newfs_super.ino_map_blks;
    newfs_super_d.data_map_offset = newfs_super.data_map_offset;
    newfs_super_d.data_map_blks = newfs_super.data_map_blks;
    newfs_super_d.ino_offset = newfs_super.ino_offset;
    newfs_super_d.ino_blks = newfs_super.ino_blks;
    newfs_super_d.data_offset = newfs_super.data_offset;
    newfs_super_d.data_blks = newfs_super.data_blks;
    newfs_super_d.sz_usage    = newfs_super.sz_usage   ;

    if (newfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (newfs_driver_write(NFS_BLKS_SZ(newfs_super_d.ino_map_offset), (uint8_t *)(newfs_super.map_inode), 
                        NFS_BLKS_SZ(newfs_super_d.ino_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (newfs_driver_write(NFS_BLKS_SZ(newfs_super_d.data_map_offset), (uint8_t *)(newfs_super.map_data), 
                        NFS_BLKS_SZ(newfs_super_d.data_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;
}
