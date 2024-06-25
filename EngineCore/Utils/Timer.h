#pragma once

#include <chrono>

namespace vz
{
	struct Timer
	{
		std::chrono::high_resolution_clock::time_point timeStamp = std::chrono::high_resolution_clock::now();

		// Record a reference timestamp
		inline void Record()
		{
			timeStamp = std::chrono::high_resolution_clock::now();
		}

		// Elapsed time in seconds between the vz::Timer creation or last recording and "timestamp2"
		inline double ElapsedSecondsSince(std::chrono::high_resolution_clock::time_point timestamp2)
		{
			std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(timestamp2 - timeStamp);
			return time_span.count();
		}

		// Elapsed time in seconds since the vz::Timer creation or last recording
		inline double ElapsedSeconds()
		{
			return ElapsedSecondsSince(std::chrono::high_resolution_clock::now());
		}

		// Elapsed time in milliseconds since the vz::Timer creation or last recording
		inline double ElapsedMilliseconds()
		{
			return ElapsedSeconds() * 1000.0;
		}

		// Elapsed time in milliseconds since the vz::Timer creation or last recording
		inline double Elapsed()
		{
			return ElapsedMilliseconds();
		}

		// Record a reference timestamp and return elapsed time in seconds since the vz::Timer creation or last recording
		inline double RecordElapsedSeconds()
		{
			auto timestamp2 = std::chrono::high_resolution_clock::now();
			auto elapsed = ElapsedSecondsSince(timestamp2);
			timeStamp = timestamp2;
			return elapsed;
		}
	};
}
