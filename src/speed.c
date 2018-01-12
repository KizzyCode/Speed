//
//  main.c
//  Speed
//
//  Created by Keziah on 02.11.15.
//  Copyright © 2015 KizzyCode. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

// Size-constants
#define KB 1000L
#define MB 1000000L
#define GB 1000000000L
#define TB 1000000000000L
#define KiB 1024L
#define MiB 1048576L
#define GiB 1073741824L
#define TiB 1099511627776L

// IO-buffer-size
#define BUFFER_SIZE ((unsigned long long)KiB * 256ULL)

// Update-interval and history-buffer
#define UPDATE_INTERVAL_MS 40ULL // The number *must* be a divisor of thousand
#define LASTIOCOUNT_BUFFER_SIZE (1000ULL / UPDATE_INTERVAL_MS)

// Thread-safe accessors for `ioCounter` (mutex) and `constants` (read-only)
#define IOCOUNTER_EXCLUSIVE_ACCESS(code) pthread_mutex_lock(&IOCounter.mutex); code; pthread_mutex_unlock(&IOCounter.mutex);
#define CONSTANTS (*((struct Constants const*)&Constants))



// Helpers

void move_and_append(unsigned long long* array, unsigned long long array_size, unsigned long long to_append) {
	for (int i = 0; i < array_size - 1; i++) array[i] = array[i + 1];
	array[array_size - 1] = to_append;
}

long double time_to_seconds(unsigned long long time_ms) {
	return (long double)time_ms / (long double)1000;
}

unsigned long long get_time_ms() {
	struct timeval time_struct;
	gettimeofday(&time_struct, NULL);
	return ((unsigned long long)time_struct.tv_sec * 1000) + ((unsigned long long)time_struct.tv_usec / 1000);
}

void format_size(long double size, char* buffer) {
	if (size < MB) sprintf(buffer, "%.03Lf KiB", size / KiB);
	else if (size < GB) sprintf(buffer, "%.03Lf MiB", size / MiB);
	else if (size < TB) sprintf(buffer, "%.03Lf GiB", size / GiB);
	else sprintf(buffer, "%.03Lf TiB", size / TiB);
}

void format_time(long double time, char* buffer) {
	unsigned long long timeULL = (unsigned long long)time;
	sprintf(buffer, "%llu:%02llu:%02llu.%03llu", timeULL / 3600, (timeULL % 3600) / 60, timeULL % 60, (unsigned long long)(time * 1000) % 1000);
}



// Typedefs and globals

struct Buffer {
	unsigned char* bytes;
	unsigned long long filled;
};
struct Buffer Buffer;

struct IOCounter {
	unsigned long long total;
	pthread_mutex_t mutex;
};
struct IOCounter IOCounter;

struct Constants {
	struct timespec update_interval;
	unsigned long long start_time_ms;
};
struct Constants Constants;


// Functions

void* show_state(void* _) {
	unsigned long long last_io_count_buffer[LASTIOCOUNT_BUFFER_SIZE] = {0};
	unsigned long long time_lost_ms = 0;
	
	while (1) {
		// Get the current time and compute the elapsed time
		unsigned long long current_time_ms = get_time_ms();
		long double time_elapsed_sec = time_to_seconds(current_time_ms - CONSTANTS.start_time_ms);
		
		// Create snapshot of state
		unsigned long long current_io_count;
		IOCOUNTER_EXCLUSIVE_ACCESS({ current_io_count = IOCounter.total; });
		
		// Update `lastIOs`; if we lost one interval, update again to skip one buffered IO-count
		move_and_append(last_io_count_buffer, LASTIOCOUNT_BUFFER_SIZE, current_io_count);
		if (time_lost_ms > UPDATE_INTERVAL_MS) {
			move_and_append(last_io_count_buffer, LASTIOCOUNT_BUFFER_SIZE, current_io_count);
			time_lost_ms = 0;
		}
		
		// Create strings
		char total_buf[64];
		format_size((long double)current_io_count, total_buf);
		
		char total_avg_buf[64];
		format_size((long double)current_io_count / time_elapsed_sec, total_avg_buf);
		
		char current_avg_buf[64];
		format_size((long double)(current_io_count - last_io_count_buffer[0]), current_avg_buf);
		
		char time_alapsed_buf[64];
		format_time(time_elapsed_sec, time_alapsed_buf);
		
		// '\33[2K' clears the current TTY-line
		fprintf(stderr, "\r\33[2K%s @ %s/s [currently: %s/s, time elapsed: %s]", total_buf, total_avg_buf, current_avg_buf, time_alapsed_buf);
		fflush(stderr);
		
		// Calculate the time we lost due to computing and printing and sleep
		time_lost_ms += get_time_ms() - current_time_ms;
		nanosleep(&CONSTANTS.update_interval, NULL);
	}
}

void* io(void* _) {
	do {
		Buffer.filled = fread(Buffer.bytes, 1, BUFFER_SIZE, stdin);
		IOCOUNTER_EXCLUSIVE_ACCESS({ IOCounter.total += Buffer.filled; });
		
		unsigned long long written = fwrite(Buffer.bytes, 1, Buffer.filled, stdout);
		if (written != Buffer.filled) {
			fprintf(stderr, "... Failed to write to stdout.\n");
			return NULL;
		}
	} while (Buffer.filled > 0);
	return NULL;
}

int main(int argc, const char** arg) {
	// Initialize
	Buffer.bytes = malloc(BUFFER_SIZE);
	if (Buffer.bytes == NULL) {
		fprintf(stderr, "!> Failed to allocate buffer.\n");
		return 1;
	}
	if (pthread_mutex_init(&IOCounter.mutex, NULL) != 0) {
		fprintf(stderr, "!> Failed to initialize mutex.\n");
		return 2;
	}
	Constants.update_interval.tv_nsec = UPDATE_INTERVAL_MS * 1000000;
	Constants.start_time_ms = get_time_ms();
	
	// Spawn threads
	pthread_t io_thread;
	if (pthread_create(&io_thread, NULL, io, NULL) != 0) {
		fprintf(stderr, "!> Failed to spawn IO-thread.\n");
		return 3;
	}
	pthread_t monitor_thread;
	if (pthread_create(&monitor_thread, NULL, show_state, NULL) != 0) {
		fprintf(stderr, "!> Failed to start monitor-thread.\n");
		return 4;
	}
	
	// Wait for IO to finish and die
	pthread_join(io_thread, NULL);
	fprintf(stderr, "\n");
	return 0;
}
