/**
 *
 * CPFS
 *
 * Copyright (C) 2016-2016 Dmitry Zavalishin, dz@dz.ru
 *
 * Test units implementation.
 *
 *
**/


#include "cpfs.h"
#include "cpfs_local.h"
#include "cpfs_defs.h"

#include "cpfs_test.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


void    test_superblock(void)
{
}


#define QSZ (2048*100)

cpfs_blkno_t    tda_q[QSZ];
int             tda_q_pp = 0;
int             tda_q_gp = 0;

static void reset_q(void)
{
    tda_q_pp = 0;
    tda_q_gp = 0;
}


static void mass_blk_alloc(int cnt)
{
    while(cnt-->0)
    {
        cpfs_blkno_t blk = cpfs_alloc_disk_block( &fs );
        if( !blk ) cpfs_panic("can't allocate block");
        if(tda_q_pp >= QSZ) cpfs_panic("test out of q");
        tda_q[tda_q_pp++] = blk;
    }
}

static void mass_blk_free(int cnt)
{
    while(cnt-->0)
    {
        cpfs_blkno_t blk = tda_q[tda_q_gp++];
        if( !blk ) cpfs_panic("mass_blk_free blk 0");
        if(tda_q_gp >= QSZ) cpfs_panic("mass_blk_free test out of q");
        cpfs_free_disk_block( &fs, blk );
    }
}


void
test_disk_alloc(void)
{
    printf("Disk block allocation test: mixed alloc/free\n");
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );

    cpfs_blkno_t initial_free = fs.sb.free_count;

    mass_blk_alloc(1);   // +
    mass_blk_alloc(120); // +
    mass_blk_free(34);   // -
    mass_blk_alloc(40);  // +
    mass_blk_free(120);  // -
    mass_blk_alloc(80);  // +
    mass_blk_free(40);   // -
    mass_blk_alloc(34);  // +
    mass_blk_free(80);   // -
    mass_blk_free(1);    // -

    if( initial_free != fs.sb.free_count )
    {
        printf("FAIL: initial_free (%lld) != fs.sb.free_count (%lld)\n", (long long)initial_free, (long long)fs.sb.free_count );
    }

    // Now do max possible run twice

    printf("Disk block allocation test: big runs\n");
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );
    reset_q();
    mass_blk_alloc(1700);
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );
    mass_blk_free(1700);
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );
    reset_q();
    mass_blk_alloc(1700);
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );
    mass_blk_free(1700);
    //printf("fs.sb.free_count = %lld\n", (long long)fs.sb.free_count );

    reset_q();

    // TODO attempt to allocate over the end of disk, check for graceful deny

    printf("Disk block allocation test: DONE\n");
}









void
test_inode_blkmap(void) 	// test file block allocation with inode
{
    errno_t rc;
    cpfs_ino_t ino;
    cpfs_blkno_t phys0;
    cpfs_blkno_t phys1;

    printf("Inode block map test: allocate inode\n");

    rc = cpfs_alloc_inode( &fs, &ino );
    if( rc ) cpfs_panic( "can't alloc inode, %d", rc );

    printf("Inode block map test: inode %lld, allocate data block\n", (long long)ino);


    rc = cpfs_alloc_block_4_file( &fs, ino, 0, &phys0 );
    if( rc ) cpfs_panic( "cpfs_alloc_block_4_file rc=%d", rc );

    rc = cpfs_find_block_4_file( &fs, ino, 0, &phys1 );
    if( rc ) cpfs_panic( "cpfs_find_block_4_file rc=%d", rc );

    if( phys0 != phys1 )
        cpfs_panic( "allocated and found blocks differ: %lld and %lld", (long long)phys0, (long long)phys1 );

    if( phys0 == 0 )
        cpfs_panic( "allocated block is zero", (long long)phys0 );

    printf("Inode block map test: allocated and found blk %lld\n", (long long)phys1);

    printf("Inode block map test: free inode\n");
    cpfs_free_inode( &fs, ino );

    // todo test sparce allocation, far after the size of file

    printf("Inode block map test: DONE\n");

}














static const char test_data[] = "big brown something jumps over a lazy programmer and runs regression tests, though quite in vain";
static char test_buf[256];

void
test_inode_io(void) 		// read/write directly with inode, no file name
{

    errno_t rc;
    cpfs_ino_t ino;

    printf("Inode io test: allocate inode\n");

    rc = cpfs_alloc_inode( &fs, &ino );
    if( rc ) cpfs_panic( "can't alloc inode, %d", rc );

    printf("Inode io test: inode %lld, write file data\n", (long long)ino);

    rc = cpfs_ino_file_write( &fs, ino, 0, test_data, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't write data, %d", rc );

    rc = cpfs_ino_file_read( &fs, ino, 0, test_buf, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't read data, %d", rc );

    if( memcmp( test_data, test_buf, sizeof(test_data) ) )
        cpfs_panic( "read data differs, '%s' and '%s'", test_data, test_buf );

    // TODO test read out of allocated part of file

    printf("Inode io test: DONE\n");

}






















static void mke( const char *name )
{
    // Write inode num -1 to dir entry for it to be extremely wrong
    errno_t rc = cpfs_alloc_dirent( &fs, 0, name, -1 );
    if( rc ) cpfs_panic( "mke %d", rc );
}

static void rme( const char *name )
{
    //errno_t rc = cpfs_free_dirent( 0, name );
    cpfs_ino_t ret;
    errno_t rc = cpfs_namei( &fs, 0, name, &ret, 1 );
    if( rc ) cpfs_panic( "rme %d", rc );
}

static void ise( const char *name )
{
    cpfs_ino_t ret;
    errno_t rc = cpfs_namei( &fs, 0, name, &ret, 0 );
    if( rc ) cpfs_panic( "ise %d", rc );
}

static void noe( const char *name )
{
    cpfs_ino_t ret;
    errno_t rc = cpfs_namei( &fs, 0, name, &ret, 0 );
    if( rc != ENOENT ) cpfs_panic( "noe %d", rc );
}


void
test_directory(void)
{
    printf("Directory entry allocation test: simpe alloc/free\n");

    mke("f1");
    ise("f1");
    rme("f1");
    noe("f1");

    printf("Directory entry allocation test: mixed alloc/free\n");

    mke("f1");
    mke("f2");
    ise("f1");
    ise("f2");

    rme("f1");
    mke("f3");
    noe("f1");
    ise("f3");

    ise("f2");
    rme("f2");
    noe("f2");

    rme("f3");
    noe("f3");

    printf("Directory entry allocation test: DONE\n");

}















void
test_file_data()        	// Create, write, close, reopen, read and compare data, in a mixed way
{

    printf("File API data test: Create files\n");

    int fd1, fd2, fd3;
    errno_t rc;

    rc = cpfs_file_open( &fs, &fd1, "test_file_1", O_CREAT, 0 );
    if( rc )  cpfs_panic( "create 1 %d", rc );

    rc = cpfs_file_open( &fs, &fd2, "test_file_2", O_CREAT, 0 );
    if( rc )  cpfs_panic( "create 2  %d", rc );



    rc = cpfs_file_write( fd1, 0, test_data, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't write data1, %d", rc );

    rc = cpfs_file_read( fd1, 0, test_buf, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't read data1, %d", rc );

    if( memcmp( test_data, test_buf, sizeof(test_data) ) )
        cpfs_panic( "read data1 differs, '%s' and '%s'", test_data, test_buf );



    rc = cpfs_file_write( fd2, 100, test_data, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't write data2, %d", rc );

    rc = cpfs_file_read( fd2, 100, test_buf, sizeof(test_data) );
    if( rc ) cpfs_panic( "can't read data2, %d", rc );

    if( memcmp( test_data, test_buf, sizeof(test_data) ) )
        cpfs_panic( "read data2 differs, '%s' and '%s'", test_data, test_buf );


    printf("File API data test: DONE\n");
}











