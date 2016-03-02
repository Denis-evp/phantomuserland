#include <kernel/config.h>

#if HAVE_NET

#define DEBUG_MSG_PREFIX "net.misc"
#include <debug_ext.h>
#define debug_level_flow 10
#define debug_level_error 10
#define debug_level_info 10

#include <kernel/libkern.h>

#include <netinet/resolv.h>

#include <kernel/net.h>
#include <kernel/net/udp.h>
#include <kernel/net/tcp.h>
//#include <netinet/tftp.h>

#include <phantom_libc.h>
#include <endian.h>
#include <phantom_time.h>
#include <time.h>
#include <sys/syslog.h>

int phantom_tcpip_active = 0;

//static void start_tcp_echo_server(void);





#define SYSLOGD_PORT 514

static void *syslog_socket = 0;
static int syslog_failed = 0;
static char syslog_src_addr[64] = "(unknown)";
static i4sockaddr syslog_addr;

// Set in boot_cmd_line
extern char *syslog_dest_address_string;

static int connect_syslog(void)
{
    if( udp_open(&syslog_socket) )
    {
        SHOW_FLOW0( 1, "UDP syslog - can't open endpoint");
        return -1;
    }

    syslog_addr.port = SYSLOGD_PORT; // local port to be the same

    syslog_addr.addr.len = 4;
    syslog_addr.addr.type = ADDR_TYPE_IP;
    NETADDR_TO_IPV4(syslog_addr.addr) = IPV4_DOTADDR_TO_ADDR(192, 168, 1, 100);

    // It won't assign if address string is null or wrong
    parse_ipv4_addr( &(NETADDR_TO_IPV4(syslog_addr.addr)), syslog_dest_address_string );


    int rc;
    if( 0 != (rc = udp_bind(syslog_socket, &syslog_addr)) )
        return rc;

    syslog_addr.port = SYSLOGD_PORT; // Remote port

#if 1
    ipv4_addr src_addr;
    if(ipv4_lookup_srcaddr_for_dest( NETADDR_TO_IPV4(syslog_addr.addr), &src_addr))
    {
        SHOW_ERROR0( 0 , "UDP logger: unable to find my ip address");
        //return -1;
    }
    else
    {
        const char *ssap = (const char *)&src_addr;
        snprintf( syslog_src_addr, sizeof(syslog_src_addr)-1, "%d.%d.%d.%d",
                  ssap[3], ssap[2], ssap[1], ssap[0] );
    }
#else
    snprintf( syslog_src_addr, sizeof(syslog_src_addr)-1, "10.0.2.123");
#endif
    return 0;
}

#if 0
void udp_syslog_send(const char *message)
{
    if(syslog_failed)
        return;
    if(syslog_socket == 0)
        if(connect_syslog())
        {
            syslog_failed = 1;
            return;
        }


    struct tm tmb;
    localtime_rb(hal_local_time(), &tmb);

    char buf[1024];
    // kernel (0), debug(7)
    snprintf( buf, sizeof(buf)-1,
              "<%d%d>%s %02d %02d:%02d:%02d %s %s",
              0, 7,
              monNames[tmb.tm_mon], tmb.tm_mday,
              tmb.tm_hour, tmb.tm_min, tmb.tm_sec,
              syslog_src_addr,
              message);

    SHOW_FLOW( 7, "UDP syslog send '%s'\n", buf);

    int rc;
    if( 0 != (rc = udp_sendto(syslog_socket, buf, strlen(buf), &syslog_addr)) )
    {
        if(rc == ERR_NET_NO_ROUTE)
        {
            SHOW_ERROR( 0 , "UDP syslog - No route\n", rc);
        }
        else
            SHOW_ERROR( 0 , "UDP syslog - can't send, rc = %d\n", rc);
    }
}
#else
void udp_syslog_send(const char *prefix, const char *message)
{
    if(syslog_failed || !phantom_tcpip_active)
        return;

    if( (syslog_socket == 0) && connect_syslog() )
    {
        syslog_failed = 1;
        return;
    }

    char buf[1024];
    // kernel (0), debug(7)
    snprintf( buf, sizeof(buf)-1,
              "%s %s %s",
              prefix,
              syslog_src_addr,
              message);

    SHOW_FLOW( 7, "UDP syslog send '%s'\n", buf);

    int rc;
    if( 0 != (rc = udp_sendto(syslog_socket, buf, strlen(buf), &syslog_addr)) )
    {
        if(rc == ERR_NET_NO_ROUTE)
        {
            SHOW_ERROR0( 1, "UDP syslog - No route\n");
        }
        else
            SHOW_ERROR( 1, "UDP syslog - can't send, rc = %d\n", rc);
    }
}

#endif





#include <kernel/net/tcp.h>
#include <kernel/init.h>
#include <threads.h>

static void tcp_echo( void *echo_socket )
{
    char buf[1024];
    i4sockaddr addr;

    while(1)
    {

        int nread = tcp_recvfrom( echo_socket, buf, sizeof(buf), &addr, SOCK_FLAG_TIMEOUT, 1000L*1000*50 );
        if( nread < 0 )
            break;

        SHOW_FLOW0( 5, "echo" );


        int nwrite = tcp_sendto( echo_socket, buf, nread, &addr);
        if( nwrite != nread )
            SHOW_ERROR( 0, "nw %d != nr %d", nwrite, nread );

    }

    SHOW_FLOW0( 0, "close data socket" );

    tcp_close( echo_socket );
}


#define ECHO_PORT 7

static void tcp_echo_thread(void *arg)
{
    (void) arg;
    void *prot_data;

    hal_sleep_msec( 1000*10 );

#if 1 // test curl
    {
        char buf[1024];
        errno_t rc = net_curl( "http://ya.ru/", buf, sizeof( buf ) );
        SHOW_FLOW( 0, "curl rc = %d, data = '%s'\n", rc, buf );
    }
#endif

    if( tcp_open(&prot_data) )
    {
        SHOW_ERROR0( 0, "can't prepare TCP endpoint" );
        return;
    }

    SHOW_FLOW0( 0, "got accept socket" );

    i4sockaddr addr;

    addr.port = ECHO_PORT;

    addr.addr.len = 4;
    addr.addr.type = ADDR_TYPE_IP;
    //NETADDR_TO_IPV4(addr.addr) = IPV4_DOTADDR_TO_ADDR(0xFF, 0xFF, 0xFF, 0xFF);
    NETADDR_TO_IPV4(addr.addr) = IPV4_DOTADDR_TO_ADDR(0, 0, 0, 0);

    int rc = tcp_bind( prot_data, &addr );
    if( rc )
    {
        SHOW_ERROR( 0, "can't bind - %d", rc );
        goto fail;
    }

    SHOW_FLOW0( 0, "bound" );

    tcp_listen(prot_data);

    SHOW_FLOW0( 0, "listen" );

    while(1)
    {
        i4sockaddr acc_addr;
        void *echo_socket;

        int arc = tcp_accept( prot_data, &acc_addr, &echo_socket );
        if( arc )
        {
            SHOW_ERROR( 0, "can't accept - %d", arc );
            goto fail;
        }

        SHOW_FLOW( 0, "accepted - %x", NETADDR_TO_IPV4(acc_addr.addr) );


        hal_start_thread( tcp_echo, echo_socket, 0 );

    }

fail:
    if( tcp_close(prot_data) )
        SHOW_ERROR0( 0, "can't close TCP endpoint" );

}

void start_tcp_echo_server(void)
{
    SHOW_FLOW0( 0, "start TCP echo server" );

    tid_t te = hal_start_thread( tcp_echo_thread, 0, 0 );
    if( te < 0 )
        SHOW_ERROR( 1, "Can't start tcp echo thread (%d)", te );


}

INIT_ME( 0, 0, start_tcp_echo_server );


#define CURL_MAXBUF 512

errno_t net_curl( const char *url, char *obuf, size_t obufsize )
{
    int nread = 0;

    errno_t rc;
    void *prot_data;
    char host[CURL_MAXBUF+1];
    char path[CURL_MAXBUF+1];

    if( strlen(url) > CURL_MAXBUF )
        return EINVAL;

#if 0
    int cnt = sscanf( url, "http://%[^/]/%s", host, path );
    if( cnt != 2 )
        return EINVAL;
#else
    if( strncmp( url, "http://", 7 ) )
        return EINVAL;

    url += 7;

    char *pos = strchr( url, '/' );
    if( pos == 0 )
        return EINVAL;

    strlcpy( host, url, pos-url+1 );
    strcpy( path, pos+1 );
#endif

    SHOW_FLOW( 1, "curl host '%s' path '%s'", host, path );

    in_addr_t out;

    rc = name2ip( &out, host, 0 );
    if( rc )
    {
        SHOW_ERROR( 0, "can't resolve '%s', %d", host, rc );
        return rc;
    }

    if( tcp_open(&prot_data) )
    {
        SHOW_ERROR0( 0, "can't prepare TCP endpoint" );
        return ENOMEM;
    }

    i4sockaddr addr;
    addr.port = 80;

    addr.addr.len = 4;
    addr.addr.type = ADDR_TYPE_IP;
    NETADDR_TO_IPV4(addr.addr) = out; //IPV4_DOTADDR_TO_ADDR(ip0, ip1, ip2, ip3);

    //SHOW_FLOW( 0, "TCP - create socket to %d.%d.%d.%d port %d", ip0, ip1, ip2, ip3, port);

    SHOW_FLOW0( 0, "TCP - will connect");
    if( tcp_connect( prot_data, &addr) )
    {
        SHOW_ERROR(0, "can't connect to %s", host);
        goto err;
    }
    SHOW_FLOW0( 0, "TCP - connected, read");

    char buf[1024];


    memset( buf, 0, sizeof(buf) );
    strlcpy( buf, "GET /", sizeof(buf) );
    strlcat( buf, path, sizeof(buf) );
    strlcat( buf, "\r\n\r\n", sizeof(buf) );

    //snprintf( buf, sizeof(buf), "GET / HTTP/1.1\r\nHost: ya.ru\r\nUser-Agent: PhantomOSNetTest/0.1 (PhanomOS i686; ru)\r\nAccept: text/html\r\nConnection: close\r\n\r\n" );
    int len = strlen(buf);
    int nwrite = tcp_sendto( prot_data, buf, len, &addr);
    SHOW_FLOW( 0, "TCP - write = %d (%s)", nwrite, buf);
    if( nwrite != len ) goto err;

    memset( buf, 0, sizeof(buf) );
    nread = tcp_recvfrom( prot_data, buf, sizeof(buf), &addr, SOCK_FLAG_TIMEOUT, 1000L*1000*50 );
    buf[sizeof(buf)-1] = 0;

    SHOW_FLOW( 0, "TCP - read = %d (%s)", nread, buf);
err:
    tcp_close(prot_data);

    if( nread <= 0 )
        return EIO;

    memset( obuf, 0, obufsize );
    len = umin( obufsize-1, nread );
    strncpy( obuf, buf, len );

    return 0;
}





#endif // HAVE_NET

