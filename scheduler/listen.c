/*
 * "$Id: listen.c,v 1.13.2.1 2005/07/27 18:22:02 jlovell Exp $"
 *
 *   Server listening routines for the Common UNIX Printing System (CUPS)
 *   scheduler.
 *
 *   Copyright 1997-2005 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636 USA
 *
 *       Voice: (301) 373-9600
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 * Contents:
 *
 *   PauseListening()  - Clear input polling on all listening sockets...
 *   ResumeListening() - Set input polling on all listening sockets...
 *   StartListening()  - Create all listening sockets...
 *   StopListening()   - Close all listening sockets...
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"

#ifdef HAVE_DOMAINSOCKETS
#  include <sys/un.h>
#endif /* HAVE_DOMAINSOCKETS */


/*
 * 'PauseListening()' - Clear input polling on all listening sockets...
 */

void
PauseListening(void)
{
  int		i;		/* Looping var */
  listener_t	*lis;		/* Current listening socket */


  if (NumListeners < 1 || !FD_ISSET(Listeners[0].fd, InputSet))
    return;

  if (NumClients == MaxClients)
    LogMessage(L_WARN, "Max clients reached, holding new connections...");

  LogMessage(L_DEBUG, "PauseListening: clearing input bits...");

  for (i = NumListeners, lis = Listeners; i > 0; i --, lis ++)
  {
    LogMessage(L_DEBUG2, "PauseListening: Removing fd %d from InputSet...",
               lis->fd);

    FD_CLR(lis->fd, InputFds);
    FD_CLR(lis->fd, InputSet);
  }
}


/*
 * 'ResumeListening()' - Set input polling on all listening sockets...
 */

void
ResumeListening(void)
{
  int		i;		/* Looping var */
  listener_t	*lis;		/* Current listening socket */


  if (NumListeners < 1 || FD_ISSET(Listeners[0].fd, InputSet))
    return;

  if (NumClients >= (MaxClients - 1))
    LogMessage(L_WARN, "Resuming new connection processing...");

  LogMessage(L_DEBUG, "ResumeListening: setting input bits...");

  for (i = NumListeners, lis = Listeners; i > 0; i --, lis ++)
  {
    LogMessage(L_DEBUG2, "ResumeListening: Adding fd %d to InputSet...",
               lis->fd);

    FD_SET(lis->fd, InputSet);
  }
}


/*
 * 'StartListening()' - Create all listening sockets...
 */

void
StartListening(void)
{
  int		i,		/* Looping var */
		val;		/* Parameter value */
  listener_t	*lis;		/* Current listening socket */
  struct hostent *host;		/* Host entry for server address */


  LogMessage(L_DEBUG, "StartListening: NumListeners=%d", NumListeners);

 /*
  * Get the server's IP address...
  */

  memset(&ServerAddr, 0, sizeof(ServerAddr));

  if ((host = httpGetHostByName(ServerName)) != NULL)
  {
   /*
    * Found the server's address!
    */

    memcpy((char *)&(ServerAddr.sin_addr), host->h_addr, host->h_length);
    ServerAddr.sin_family = host->h_addrtype;
  }
  else
  {
   /*
    * Didn't find it!  Use an address of 0...
    */

    LogMessage(L_ERROR, "StartListening: Unable to find IP address for server name \"%s\" - %s\n",
               ServerName, hstrerror(h_errno));

    ServerAddr.sin_family = AF_INET;
  }

 /*
  * Setup socket listeners...
  */

  for (i = NumListeners, lis = Listeners, LocalPort = 0; i > 0; i --, lis ++)
  {
#ifdef HAVE_DOMAINSOCKETS
    if (lis->address.sin_family == AF_LOCAL)
      LogMessage(L_DEBUG, "StartListening: domain socket=%s", (char*)lis->address.sin_addr.s_addr);
    else
#endif /* HAVE_DOMAINSOCKETS */
    LogMessage(L_DEBUG, "StartListening: address=%08x port=%d",
               (unsigned)ntohl(lis->address.sin_addr.s_addr),
	       ntohs(lis->address.sin_port));

   /*
    * Save the first port that is bound to the local loopback or
    * "any" address...
    */

    if (!LocalPort &&
        lis->address.sin_family == AF_INET &&
        (ntohl(lis->address.sin_addr.s_addr) == 0x7f000001 ||
         ntohl(lis->address.sin_addr.s_addr) == 0x00000000))
    {
      LocalPort       = ntohs(lis->address.sin_port);
      LocalEncryption = lis->encryption;
    }

   /*
    * Create a socket for listening...
    */

    if ((lis->fd = socket(lis->address.sin_family, SOCK_STREAM, 0)) == -1)
    {
      LogMessage(L_ERROR, "StartListening: Unable to open listen socket for address %08x:%d - %s.",
                 (unsigned)ntohl(lis->address.sin_addr.s_addr),
		 ntohs(lis->address.sin_port), strerror(errno));
      exit(errno);
    }

    fcntl(lis->fd, F_SETFD, fcntl(lis->fd, F_GETFD) | FD_CLOEXEC);

   /*
    * Set things up to reuse the local address for this port.
    */

    val = 1;
#ifdef __sun
    setsockopt(lis->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof(val));
#else
    setsockopt(lis->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
#endif /* __sun */

   /*
    * Bind to the port we found...
    */

    if (lis->address.sin_family == AF_INET)
    {
      if (bind(lis->fd, (struct sockaddr *)&(lis->address), sizeof(lis->address)) < 0)
      {
	LogMessage(L_ERROR, "StartListening: Unable to bind socket for address %08x:%d - %s.",
                   (unsigned)ntohl(lis->address.sin_addr.s_addr),
		   ntohs(lis->address.sin_port), strerror(errno));
	exit(errno);
      }
    }
#ifdef HAVE_DOMAINSOCKETS
    else if (lis->address.sin_family == AF_LOCAL)
    {
      struct sockaddr_un laddr;
      mode_t		 mask;

      bzero(&laddr, sizeof(laddr));
      laddr.sun_family = AF_LOCAL;
      strlcpy(laddr.sun_path, *(char **)&lis->address.sin_addr, sizeof(laddr.sun_path));
      unlink(laddr.sun_path);
      mask = umask(0);
      if (bind(lis->fd, (struct sockaddr *)&laddr, SUN_LEN(&laddr)) < 0)
      {
	LogMessage(L_ERROR, "StartListening: Unable to bind socket for address %s - %s.",
                   laddr.sun_path, strerror(errno));
	exit(errno);
      }
      umask(mask);
    }
#endif /* HAVE_DOMAINSOCKETS */
    else
    {
      LogMessage(L_ERROR, "StartListening: Unknown address family %d.",
                   (int)lis->address.sin_family);
      exit(errno);
    }

   /*
    * Listen for new clients.
    */

    if (listen(lis->fd, ListenBackLog) < 0)
    {
      LogMessage(L_ERROR, "StartListening: Unable to listen for clients on address %08x:%d - %s.",
                 (unsigned)ntohl(lis->address.sin_addr.s_addr),
		 ntohs(lis->address.sin_port), strerror(errno));
      exit(errno);
    }
  }

 /*
  * Make sure that we are listening on localhost!
  */

  if (!LocalPort)
  {
    LogMessage(L_EMERG, "No Listen or Port lines were found to allow access via localhost!");

   /*
    * Commit suicide...
    */

    kill(getpid(), SIGTERM);
  }

  ResumeListening();
}


/*
 * 'StopListening()' - Close all listening sockets...
 */

void
StopListening(void)
{
  int		i;		/* Looping var */
  listener_t	*lis;		/* Current listening socket */


  LogMessage(L_DEBUG, "StopListening: closing all listen sockets.");

  PauseListening();

  for (i = NumListeners, lis = Listeners; i > 0; i --, lis ++)
  {
#ifdef WIN32
    closesocket(lis->fd);
#else
    close(lis->fd);
#endif /* WIN32 */

#ifdef HAVE_DOMAINSOCKETS
    if (lis->address.sin_family == AF_LOCAL)
      unlink(*(char **)&lis->address.sin_addr);
#endif /* HAVE_DOMAINSOCKETS */
  }
}


/*
 * End of "$Id: listen.c,v 1.13.2.1 2005/07/27 18:22:02 jlovell Exp $".
 */
