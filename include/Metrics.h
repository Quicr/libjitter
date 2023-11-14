#ifndef LIBJITTER_METRICS_H
#define LIBJITTER_METRICS_H

/// @brief Structure defining metrics LibJitter reports.
struct Metrics {
  /// @brief Number of frames concealed due to a discontinuity.
  unsigned long concealed_packets;
  /// @brief Number of frames skipped due to expiry.
  unsigned long skipped_frames;
  /// @brief Number of frames concealed to fill to minimum depth.
  unsigned long filled_packets;
  /// @brief Number of concealment frames updated to real data.
  unsigned long updated_frames;
  /// @brief Number of real frames that arrived too late to be used to update concealment data.
  unsigned long update_missed_frames;
};

#endif
