/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "AP_RCProtocol_config.h"

#if AP_RCPROTOCOL_IBUS_ENABLED

#include "AP_RCProtocol_IBUS.h"

static int ia6_hack = 0;

//#define DEBUG_IBUS 1
//static int fd = 0;

// decode a full IBUS frame
bool ibus_decode_std(const uint8_t frame[IBUS_FRAME_SIZE], uint16_t *values, bool *ibus_failsafe)
{
    uint16_t chksum = 96;

    /* check frame boundary markers to avoid out-of-sync cases */
    if ((frame[0] != 0x20) || (frame[1] != 0x40)) {
        return false;
    }

    /* use the decoder matrix to extract channel data */
    for (uint8_t channel = 0, pick=2; channel < IBUS_INPUT_CHANNELS; channel++, pick+=2) {
        values[channel]=frame[pick]|(frame[pick+1] & 0x0F)<<8;
        chksum+=frame[pick]+frame[pick+1];
    }

    chksum += frame[IBUS_FRAME_SIZE-2]|frame[IBUS_FRAME_SIZE-1]<<8;

    if (chksum!=0xFFFF) {

#ifdef DEBUG_IBUS
        if (! (fd & 2)) {
            printf("ibus_decode_std chksum err %04x\n", chksum);
            fd |= 2;
        }
#endif
        return false;
    }

    if ((frame[3]&0xF0) || (frame[9]&0xF0)) {
        *ibus_failsafe = true;
    } else {
        *ibus_failsafe = false;
    }

    return true;
}


// decode a full IBUS frame
bool ibus_decode_ia6(const uint8_t frame[IBUS_FRAME_SIZE], uint16_t *values, bool *ibus_failsafe)
{
    uint16_t chksum = 0;

    /* check frame boundary markers to avoid out-of-sync cases */
    if (frame[0] != 0x55) {
        return false;
    }

    /* use the decoder matrix to extract channel data */
    for (uint8_t channel = 0, pick=1; channel < IBUS_INPUT_CHANNELS; channel++, pick+=2) {
        values[channel] = frame[pick] | ((frame[pick+1] & 0x0F)<<8);
        chksum += values[channel];
    }

    uint16_t fr_chksum = frame[29] | (frame[30]<<8);

    if (chksum != fr_chksum) {

//#ifdef DEBUG_IBUS
//        if (! (fd & 2)) {
            printf("ibus_decode_ia6 chksum err %04x != %04x \n", chksum, fr_chksum);
//            fd |= 2;
//       }
//endif
        return false;
    }
#ifdef DEBUG_IBUS
    printf("ibus_decode_ia6 chksum ok\n");
#endif

    if ((frame[2]&0xF0) || (frame[8]&0xF0)) {
        *ibus_failsafe = true;
#ifdef DEBUG_IBUS
        if (! (fd & 4)) {
            printf("ibus_decode_ia6 ibus_failsafe = true \n" );
            fd |= 4;
        }
#endif
    } else {
        *ibus_failsafe = false;
    }

    return true;
}

bool AP_RCProtocol_IBUS::ibus_decode(const uint8_t frame[IBUS_FRAME_SIZE], uint16_t *values, bool *ibus_failsafe)
{
    if (ia6_hack) {
        return ibus_decode_ia6( frame, values, ibus_failsafe);
    }
    return ibus_decode_std( frame, values, ibus_failsafe);
}

/*
  process an IBUS input pulse of the given width
 */
void AP_RCProtocol_IBUS::process_pulse(uint32_t w0, uint32_t w1)
{
    uint8_t b;
    if (ss.process_pulse(w0, w1, b)) {
        _process_byte(ss.get_byte_timestamp_us(), b);
    }
}

// support byte input
void AP_RCProtocol_IBUS::_process_byte(uint32_t timestamp_us, uint8_t b)
{
    const bool have_frame_gap = (timestamp_us - byte_input.last_byte_us >= 500U);
    byte_input.last_byte_us = timestamp_us;


    if (have_frame_gap) {
        // if we have a frame gap then this must be the start of a new
        // frame
        byte_input.ofs = 0;

#ifdef DEBUG_IBUS        
        printf("\nAP_RCProtocol_IBUS::_process_byte: timestamp_us = %d; b = %02x ", timestamp_us, b);
    }
    else {
         printf("%02x ", b);
#endif
    }

    if ( !( (b == 0x20) || (b == 0x55) ) && byte_input.ofs == 0) {
        // definitely not IBUS, missing header byte
        return;
    }

    if (byte_input.ofs == 0 && !have_frame_gap) {
        // must have a frame gap before the start of a new IBUS frame
        return;
    }

    if (byte_input.ofs == 0)
    {
        ia6_hack = (b == 0x55)?1:0;
#ifdef DEBUG_IBUS
        if (! (fd & 1)) {
            printf("ia6_hack %d\n", ia6_hack);
            fd |= 1;
        }
#endif
    }
  
    byte_input.buf[byte_input.ofs++] = b;

    if (byte_input.ofs == (sizeof(byte_input.buf)-ia6_hack)) // IA6 has one byte less
    {
        uint16_t values[IBUS_INPUT_CHANNELS];
        bool ibus_failsafe = false;
        log_data(AP_RCProtocol::IBUS, timestamp_us, byte_input.buf, byte_input.ofs);
        if (ibus_decode(byte_input.buf, values, &ibus_failsafe)) {
            add_input(IBUS_INPUT_CHANNELS, values, ibus_failsafe);
        }
        byte_input.ofs = 0;
    }
}

// support byte input
void AP_RCProtocol_IBUS::process_byte(uint8_t b, uint32_t baudrate)
{
    if (baudrate != 115200) {
        return;
    }
    _process_byte(AP_HAL::micros(), b);
}

#endif  // AP_RCPROTOCOL_IBUS_ENABLED
