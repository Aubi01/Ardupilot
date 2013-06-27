// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-



#include <AP_Common.h>
#include <AP_Math.h>
#include <AP_HAL.h>
#include "GPS.h"

extern const AP_HAL::HAL& hal;

#define GPS_DEBUGGING 0

#if GPS_DEBUGGING
 # define Debug(fmt, args ...)  do {hal.console->printf("%s:%d: " fmt "\n", __FUNCTION__, __LINE__, ## args); hal.scheduler->delay(0); } while(0)
#else
 # define Debug(fmt, args ...)
#endif

GPS::GPS(void) :
	// ensure all the inherited fields are zeroed
	time(0),
	num_sats(0),
	new_data(false),
	fix(FIX_NONE),
	valid_read(false),
	last_fix_time(0),
	_have_raw_velocity(false),
	_idleTimer(0),
	_status(GPS::NO_FIX),
	_last_ground_speed_cm(0),
	_velocity_north(0),
	_velocity_east(0),
	_velocity_down(0)

{
}

void
GPS::update(void)
{
    bool result;
    uint32_t tnow;

    // call the GPS driver to process incoming data
    result = read();

    tnow = hal.scheduler->millis();

    // if we did not get a message, and the idle timer of 1.2 seconds has expired, re-init
    if (!result) {
        if ((tnow - _idleTimer) > 1200) {
            Debug("gps read timeout %lu %lu", (unsigned long)tnow, (unsigned long)_idleTimer);
            _status = NO_GPS;

            init(_port, _nav_setting);
            // reset the idle timer
            _idleTimer = tnow;
        }
    } else {
        // we got a message, update our status correspondingly
        if (fix == FIX_3D) {
            _status = GPS_OK_FIX_3D;
        }else if (fix == FIX_2D) {
            _status = GPS_OK_FIX_2D;
        }else{
            _status = NO_FIX;
        }

        valid_read = true;
        new_data = true;

        // reset the idle timer
        _idleTimer = tnow;

        if (_status >= GPS_OK_FIX_2D) {
            last_fix_time = _idleTimer;
            _last_ground_speed_cm = ground_speed;

            if (_have_raw_velocity) {
                // the GPS is able to give us velocity numbers directly
                _velocity_north = _vel_north * 0.01f;
                _velocity_east  = _vel_east * 0.01f;
                _velocity_down  = _vel_down * 0.01f;
            } else {
                float gps_heading = ToRad(ground_course * 0.01f);
                float gps_speed   = ground_speed * 0.01f;
                float sin_heading, cos_heading;

                cos_heading = cosf(gps_heading);
                sin_heading = sinf(gps_heading);

                _velocity_north = gps_speed * cos_heading;
                _velocity_east  = gps_speed * sin_heading;

				// no good way to get descent rate
				_velocity_down  = 0;
            }
        }
    }
}

void
GPS::setHIL(uint32_t _time, float _latitude, float _longitude, float _altitude,
            float _ground_speed, float _ground_course, float _speed_3d, uint8_t _num_sats)
{
}

// XXX this is probably the wrong way to do it, too
void
GPS::_error(const char *msg)
{
    hal.console->println(msg);
}

///
/// write a block of configuration data to a GPS
///
void GPS::_write_progstr_block(AP_HAL::UARTDriver *_fs, const prog_char *pstr, uint8_t size)
{
    while (size--) {
        _fs->write(pgm_read_byte(pstr++));
    }
}

/*
  a prog_char block queue, used to send out config commands to a GPS
  in 16 byte chunks. This saves us having to have a 128 byte GPS send
  buffer, while allowing us to avoid a long delay in sending GPS init
  strings while waiting for the GPS auto detection to happen
 */

// maximum number of pending progstrings
#define PROGSTR_QUEUE_SIZE 3

struct progstr_queue {
	const prog_char *pstr;
	uint8_t ofs, size;
};

static struct {
    AP_HAL::UARTDriver *fs;
	uint8_t queue_size;
	uint8_t idx, next_idx;
	struct progstr_queue queue[PROGSTR_QUEUE_SIZE];
} progstr_state;

void GPS::_send_progstr(AP_HAL::UARTDriver *_fs, const prog_char *pstr, uint8_t size)
{
	progstr_state.fs = _fs;
	struct progstr_queue *q = &progstr_state.queue[progstr_state.next_idx];
	q->pstr = pstr;
	q->size = size;
	q->ofs = 0;
	progstr_state.next_idx++;
	if (progstr_state.next_idx == PROGSTR_QUEUE_SIZE) {
		progstr_state.next_idx = 0;
	}
}

void GPS::_update_progstr(void)
{
	struct progstr_queue *q = &progstr_state.queue[progstr_state.idx];
	// quick return if nothing to do
	if (q->size == 0 || progstr_state.fs->tx_pending()) {
		return;
	}
	uint8_t nbytes = q->size - q->ofs;
	if (nbytes > 16) {
		nbytes = 16;
	}
	//hal.console->printf_P(PSTR("writing %u bytes\n"), (unsigned)nbytes);
	_write_progstr_block(progstr_state.fs, q->pstr+q->ofs, nbytes);
	q->ofs += nbytes;
	if (q->ofs == q->size) {
		q->size = 0;
		progstr_state.idx++;
		if (progstr_state.idx == PROGSTR_QUEUE_SIZE) {
			progstr_state.idx = 0;
		}
	}
}

int32_t GPS::_swapl(const void *bytes) const
{
    const uint8_t       *b = (const uint8_t *)bytes;
    union {
        int32_t v;
        uint8_t b[4];
    } u;

    u.b[0] = b[3];
    u.b[1] = b[2];
    u.b[2] = b[1];
    u.b[3] = b[0];

    return(u.v);
}

int16_t GPS::_swapi(const void *bytes) const
{
    const uint8_t       *b = (const uint8_t *)bytes;
    union {
        int16_t v;
        uint8_t b[2];
    } u;

    u.b[0] = b[1];
    u.b[1] = b[0];

    return(u.v);
}

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
  * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
  * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
  *
  * [For the Julian calendar (which was used in Russia before 1917,
  * Britain & colonies before 1752, anywhere else before 1582,
  * and is still in use by some communities) leave out the
  * -year/100+year/400 terms, and add 10.]
  *
  * This algorithm was first published by Gauss (I think).
  *
  * WARNING: this function will overflow on 2106-02-07 06:28:16 on
  * machines where long is 32-bit! (However, as time_t is signed, we
  * will already get problems at other places on 2038-01-19 03:14:08)
  */
uint64_t  GPS::_mktime(const unsigned int year0, const unsigned int mon0,
		        const unsigned int day, const unsigned int hour,
		        const unsigned int min, const unsigned int sec)
		 {
		         unsigned int mon = mon0, year = year0;

		         /* 1..12 -> 11,12,1..10 */
		         if (0 >= (int) (mon -= 2)) {
		                 mon += 12;      /* Puts Feb last since it has leap day */
		                 year -= 1;
		         }

		         return ((((uint64_t)
		                   (year/(uint64_t)4 - year/(uint64_t)100 + year/(uint64_t)400 + (uint64_t)367*mon/(uint64_t)12 + day) +
		                   year*(uint64_t)365 - (uint64_t)719499
		             )*(uint64_t)24 + hour /* now have hours */
		           )*(uint64_t)60 + min /* now have minutes */
		         )*(uint64_t)60 + sec; /* finally seconds */
		 }

