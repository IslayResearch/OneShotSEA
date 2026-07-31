#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

#include <gmpxx.h>

namespace oneshotsea {

inline constexpr std::uint32_t kSearchCheckpointVersion = 1;

struct SearchRange {
    std::uint64_t first = 0;
    std::uint64_t end = 0;

    bool empty() const { return first == end; }
    std::uint64_t size() const {
        if (first > end) {
            throw std::logic_error("reversed search range has no size");
        }
        return end - first;
    }

    bool operator==(const SearchRange&) const = default;
};

// Split [global.first, global.end) into contiguous, pairwise-disjoint shards.
// The first (global.size() % worker_count) workers receive one extra index.
SearchRange partition_search_range(SearchRange global, std::uint64_t worker_id,
                                   std::uint64_t worker_count);

struct SearchIdentity {
    mpz_class prime;
    std::uint64_t seed = 0;
    std::uint64_t worker_id = 0;
    std::uint64_t worker_count = 1;
    SearchRange range;
    std::string schedule_sha256;
    std::string table_manifest_sha256;
    std::string build_id;

    bool operator==(const SearchIdentity&) const = default;
};

enum class CurveTerminalStage : std::uint8_t {
    rejected_invalid_curve,
    rejected_sea,
    rejected_sound_early_abort,
    rejected_heuristic,
    rejected_certificate_assembly,
    completed_without_certificate,
    verified_certificate_found,
};

// This deliberately contains no trace residues or partially checked SEA state.
// A processor reports an outcome only after it has finished the indicated
// curve; `verified_certificate_found` means that exact-order construction and
// the canonical verifier have succeeded, not merely that an order looks
// promising. If the processor throws, the cursor remains at that curve for
// deterministic recomputation after restart.
struct CurveSearchOutcome {
    CurveTerminalStage terminal_stage;
    bool full_point_count_completed = false;
    bool reached_smoothness_testing = false;
};

struct SearchCounters {
    std::uint64_t curves_attempted = 0;
    std::uint64_t rejected_invalid_curve = 0;
    std::uint64_t rejected_sea = 0;
    std::uint64_t rejected_sound_early_abort = 0;
    std::uint64_t rejected_heuristic = 0;
    std::uint64_t rejected_certificate_assembly = 0;
    std::uint64_t completed_without_certificate = 0;
    std::uint64_t full_point_counts_completed = 0;
    std::uint64_t candidates_reaching_smoothness = 0;
    std::uint64_t certificates_found = 0;

    bool operator==(const SearchCounters&) const = default;
};

class SearchCheckpointError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SearchState {
public:
    explicit SearchState(SearchIdentity identity);

    const SearchIdentity& identity() const { return identity_; }
    std::uint64_t next_index() const { return next_index_; }
    const SearchCounters& counters() const { return counters_; }
    bool complete() const { return next_index_ == identity_.range.end; }

    // Only the current next_index may be completed. This prevents a worker
    // from silently skipping an index or double-counting a replayed curve.
    void record_completed(std::uint64_t index, const CurveSearchOutcome& outcome);

private:
    SearchIdentity identity_;
    std::uint64_t next_index_ = 0;
    SearchCounters counters_;

    friend SearchState load_search_checkpoint(
        const std::filesystem::path&, const SearchIdentity&);
    friend void save_search_checkpoint(const SearchState&,
                                       const std::filesystem::path&);
    friend std::string search_progress_json(const SearchState&);
};

using CurveSearchProcessor =
    std::function<CurveSearchOutcome(std::uint64_t global_index)>;

// Process at most max_curves with O(1) scheduler state. The processor is
// invoked in increasing global-index order. A processor exception leaves the
// current index uncommitted and is propagated to the caller.
std::uint64_t run_search_chunk(SearchState& state, std::uint64_t max_curves,
                               const CurveSearchProcessor& processor);

// The checkpoint is canonical JSON with a CRC64-ECMA checksum. The target file
// is replaced only after a complete temporary file has been flushed. Loading
// validates the checksum, every state invariant, and exact identity equality;
// a checkpoint from another target/range/build/schedule/table set is rejected.
void save_search_checkpoint(const SearchState& state,
                            const std::filesystem::path& checkpoint_path);
SearchState load_search_checkpoint(const std::filesystem::path& checkpoint_path,
                                   const SearchIdentity& expected_identity);

// All potentially 64-bit values are decimal strings so downstream JSON tools
// cannot lose precision. The result is one JSON object without a trailing
// newline, suitable for an NDJSON progress stream.
std::string search_progress_json(const SearchState& state);
void append_search_progress_jsonl(const SearchState& state,
                                  const std::filesystem::path& progress_path);

}  // namespace oneshotsea
