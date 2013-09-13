/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/dev/hfa/fore_include.h,v 1.2 1999/08/28 00:41:50 peter Exp $
 */

/*
 * FORE Systems 200-Series Adapter Support
 * ---------------------------------------
 *
 * Local driver include files and global declarations
 *
 */

#ifndef _FORE_INCLUDE_H
#define _FORE_INCLUDE_H

#include <sys/bus.h>

#include <netproto/atm/kern_include.h>

/*
 * If not specified elsewhere, guess which type of bus support we want
 */
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "fore.h"
#include "fore_aali.h"
#include "fore_slave.h"
#include "fore_stats.h"
#include "fore_var.h"

/*
 * Global function declarations
 */
	/* fore_buffer.c */
int		fore_buf_allocate (Fore_unit *);
void		fore_buf_initialize (Fore_unit *);
void		fore_buf_supply (Fore_unit *);
void		fore_buf_free (Fore_unit *);

	/* fore_command.c */
int		fore_cmd_allocate (Fore_unit *);
void		fore_cmd_initialize (Fore_unit *);
void		fore_cmd_drain (Fore_unit *);
void		fore_cmd_free (Fore_unit *);

	/* fore_if.c */
int		fore_atm_ioctl (int, caddr_t, caddr_t);
void		fore_interface_free (Fore_unit *);

	/* fore_init.c */
void		fore_initialize(void *);
void		fore_initialize_complete (Fore_unit *);

	/* fore_intr.c */
void		fore_intr (void *);
void		fore_watchdog (Fore_unit *);

	/* fore_load.c */

	/* fore_output.c */
void		fore_output (Cmn_unit *, Cmn_vcc *, KBuffer *);

	/* fore_receive.c */
int		fore_recv_allocate (Fore_unit *);
void		fore_recv_initialize (Fore_unit *);
void		fore_recv_drain (Fore_unit *);
void		fore_recv_free (Fore_unit *);

	/* fore_stats.c */
int		fore_get_stats (Fore_unit *);

	/* fore_timer.c */
void		fore_timeout (struct atm_time *);

	/* fore_transmit.c */
int		fore_xmit_allocate (Fore_unit *);
void		fore_xmit_initialize (Fore_unit *);
void		fore_xmit_drain (Fore_unit *);
void		fore_xmit_free (Fore_unit *);

	/* fore_vcm.c */
int		fore_instvcc (Cmn_unit *, Cmn_vcc *);
int		fore_openvcc (Cmn_unit *, Cmn_vcc *);
int		fore_closevcc (Cmn_unit *, Cmn_vcc *);


/*
 * Global variable declarations
 */
extern Fore_device	fore_devices[];
extern Fore_unit	*fore_units[];
extern int		fore_nunits;
extern struct stack_defn	*fore_services;
extern struct sp_info	fore_nif_pool;
extern struct sp_info	fore_vcc_pool;
extern struct atm_time	fore_timer;

#endif	/* _FORE_INCLUDE_H */
