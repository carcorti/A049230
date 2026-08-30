#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/*
 * Exact enumeration of the genuinely three-dimensional coefficient used to
 * reconstruct OEIS A049230.
 *
 * The raw result p3(n) = p_(n,2)^(3) counts full-cubic-group symmetry classes
 * of n-step self-avoiding walks on Z^3 that use all three coordinate axes and
 * have exactly two contacts.  A contact is an adjacency in the cubic lattice
 * between two nonconsecutive vertices.  Reversal of the order of the walk is
 * not identified.  A separate fail-closed derivator reconstructs
 * A049230(n) = 3*A033323(n) + 48*p3(n); this program neither embeds nor reads
 * the planar A033323 data.
 *
 * Canonical orientation:
 *   - the first step is +x;
 *   - the first excursion in y is positive;
 *   - a z step is allowed only after a positive y excursion;
 *   - the first excursion in z is positive.
 *
 * Every genuinely three-dimensional orbit under the 48 cubic rotations and
 * reflections has exactly one such representative.  Contacts are accumulated
 * when a vertex is appended, and a branch is pruned as soon as it has more
 * than two contacts.
 *
 * The mathematical normalization is the dimensional decomposition of
 * A. M. Nemirovsky et al., J. Stat. Phys. 67 (1992), 1083-1108.
 */

enum {
  MAX_STEPS = 29,
  MAX_SPLIT_DEPTH = 10,
  MAX_THREADS = 16,
  DEFAULT_SPLIT_DEPTH = 10,
  DEFAULT_UNIT_SIZE = 64,
  DEFAULT_SEGMENT_SECONDS = 4500,
  MAX_RECOVERY_UNITS = 4096,
  MAX_MANIFEST_BYTES = 8 * 1024 * 1024,
  HEARTBEAT_SECONDS = 300,
  HANDLED_STOP_EXIT = 2
};

/* Every genuinely 3D orbit has 48 elements.  The nonbacktracking bound gives
 * p3(n) <= 5^(n-1)/8, which is below UINT64_MAX through n=29 but not n=30. */
_Static_assert(MAX_STEPS <= 29, "uint64_t count proof ends at n=29");
_Static_assert(MAX_STEPS <= INT8_MAX, "coordinates must fit in int8_t");

#define MANIFEST_MAGIC "A049230-MANIFEST"
#define PROGRAM_ID "A049230"

#ifndef A049230_SOURCE_SHA
#define A049230_SOURCE_SHA "UNRECORDED"
#endif

#ifndef A049230_MAKEFILE_SHA
#define A049230_MAKEFILE_SHA "UNRECORDED"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(format_index, first_argument) \
  __attribute__((format(printf, format_index, first_argument)))
#else
#define PRINTF_LIKE(format_index, first_argument)
#endif

typedef struct {
  int8_t x[MAX_STEPS + 1];
  int8_t y[MAX_STEPS + 1];
  int8_t z[MAX_STEPS + 1];
  uint8_t contacts;
  bool seen_positive_y;
  bool seen_positive_z;
} Task;

typedef struct {
  Task *items;
  size_t size;
  size_t capacity;
} TaskVector;

typedef struct {
  int max_steps;
  int split_depth;
  int side;
  int offset;
  ptrdiff_t neighbour_delta[6];
  size_t grid_size;
  int8_t x[MAX_STEPS + 1];
  int8_t y[MAX_STEPS + 1];
  int8_t z[MAX_STEPS + 1];
  uint8_t *occupied;
  uint64_t prefix_counts[MAX_STEPS + 1];
  TaskVector tasks;
} BuildContext;

typedef struct {
  int max_steps;
  int side;
  int offset;
  ptrdiff_t neighbour_delta[6];
  int8_t x[MAX_STEPS + 1];
  int8_t y[MAX_STEPS + 1];
  int8_t z[MAX_STEPS + 1];
  uint8_t *occupied;
} SearchContext;

typedef struct {
  size_t task_begin;
  size_t task_end;
  unsigned segment_id;
  double elapsed_seconds;
  uint64_t counts[MAX_STEPS + 1];
} UnitRecord;

typedef struct {
  char source_sha[65];
  char makefile_sha[65];
  int max_steps;
  int split_depth;
  int threads;
  size_t unit_size;
  size_t task_count;
  size_t unit_count;
  unsigned segment_seconds;
  uint64_t generation;
  unsigned segment_id;
  bool segment_open;
  double segment_elapsed;
  size_t completed_units;
  UnitRecord *units;
} Manifest;

typedef enum {
  COMMAND_COMPUTE,
  COMMAND_INIT,
  COMMAND_NEXT,
  COMMAND_RESUME,
  COMMAND_STATUS,
  COMMAND_VERIFY
} Command;

typedef struct {
  Command command;
  int max_steps;
  int threads;
  int split_depth;
  size_t unit_size;
  unsigned segment_seconds;
  const char *state_directory;
  const char *verification_path;
} Options;

static volatile sig_atomic_t stop_requested = 0;

static const int DX[6] = {1, -1, 0, 0, 0, 0};
static const int DY[6] = {0, 0, 1, -1, 0, 0};
static const int DZ[6] = {0, 0, 0, 0, 1, -1};

static double wall_time(void) {
#ifdef _OPENMP
  return omp_get_wtime();
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0.0;
  }
  return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
#endif
}

static size_t grid_index(const int side, const int offset,
                         const int x, const int y, const int z) {
  const size_t ix = (size_t) (x + offset);
  const size_t iy = (size_t) (y + offset);
  const size_t iz = (size_t) (z + offset);
  return (ix * (size_t) side + iy) * (size_t) side + iz;
}

static bool task_vector_push(TaskVector *vector, const BuildContext *context,
                             const uint8_t contacts,
                             const bool seen_positive_y,
                             const bool seen_positive_z) {
  if (vector->size == vector->capacity) {
    const size_t new_capacity = vector->capacity == 0 ? 4096 : 2 * vector->capacity;
    if (new_capacity < vector->capacity || new_capacity > SIZE_MAX / sizeof(*vector->items)) {
      return false;
    }
    Task *new_items = realloc(vector->items, new_capacity * sizeof(*vector->items));
    if (new_items == NULL) {
      return false;
    }
    vector->items = new_items;
    vector->capacity = new_capacity;
  }

  Task *task = &vector->items[vector->size++];
  memcpy(task->x, context->x, sizeof(task->x));
  memcpy(task->y, context->y, sizeof(task->y));
  memcpy(task->z, context->z, sizeof(task->z));
  task->contacts = contacts;
  task->seen_positive_y = seen_positive_y;
  task->seen_positive_z = seen_positive_z;
  return true;
}

static bool candidate_is_valid(const uint8_t *occupied,
                               const ptrdiff_t candidate_index,
                               const ptrdiff_t neighbour_delta[6],
                               const int ny, const int nz,
                               const uint8_t contacts,
                               const bool seen_positive_y,
                               const bool seen_positive_z,
                               uint8_t *new_contacts) {
  if (occupied[(size_t) candidate_index] != 0) {
    return false;
  }

  /* Fix reflection symmetry independently in the y and z directions. */
  if (ny < 0 && !seen_positive_y) {
    return false;
  }
  if (nz != 0 && !seen_positive_y) {
    return false;
  }
  if (nz < 0 && !seen_positive_z) {
    return false;
  }

  unsigned occupied_neighbours = 0;
  for (int direction = 0; direction < 6; ++direction) {
    occupied_neighbours += occupied[(size_t) (candidate_index
                                             + neighbour_delta[direction])] != 0;
  }

  /* The predecessor is one occupied neighbour and is not a contact. */
  if (occupied_neighbours == 0) {
    return false;
  }
  const unsigned total_contacts = (unsigned) contacts + occupied_neighbours - 1U;
  if (total_contacts > 2U) {
    return false;
  }
  *new_contacts = (uint8_t) total_contacts;
  return true;
}

static bool build_prefixes(BuildContext *context, const int steps,
                           const uint8_t contacts,
                           const bool seen_positive_y,
                           const bool seen_positive_z,
                           const ptrdiff_t current_index) {
  if (steps == context->split_depth) {
    return task_vector_push(&context->tasks, context, contacts,
                            seen_positive_y, seen_positive_z);
  }

  if (contacts == 2 && seen_positive_z) {
    ++context->prefix_counts[steps];
  }

  const int x = context->x[steps];
  const int y = context->y[steps];
  const int z = context->z[steps];

  for (int direction = 0; direction < 6; ++direction) {
    const int nx = x + DX[direction];
    const int ny = y + DY[direction];
    const int nz = z + DZ[direction];
    const ptrdiff_t candidate_index = current_index
                                    + context->neighbour_delta[direction];
    uint8_t next_contacts = 0;
    if (!candidate_is_valid(context->occupied, candidate_index,
                            context->neighbour_delta, ny, nz, contacts,
                            seen_positive_y, seen_positive_z,
                            &next_contacts)) {
      continue;
    }

    const int next_steps = steps + 1;
    context->x[next_steps] = (int8_t) nx;
    context->y[next_steps] = (int8_t) ny;
    context->z[next_steps] = (int8_t) nz;
    context->occupied[(size_t) candidate_index] = 1;
    const bool ok = build_prefixes(context, next_steps, next_contacts,
                                   seen_positive_y || ny > 0,
                                   seen_positive_z || nz > 0,
                                   candidate_index);
    context->occupied[(size_t) candidate_index] = 0;
    if (!ok) {
      return false;
    }
  }
  return true;
}

static void search_task(SearchContext *context, const int steps,
                        const uint8_t contacts,
                        const bool seen_positive_y,
                        const bool seen_positive_z,
                        const ptrdiff_t current_index,
                        uint64_t counts[MAX_STEPS + 1]) {
  if (contacts == 2 && seen_positive_z) {
    ++counts[steps];
  }
  if (steps == context->max_steps) {
    return;
  }

  const int x = context->x[steps];
  const int y = context->y[steps];
  const int z = context->z[steps];
  for (int direction = 0; direction < 6; ++direction) {
    const int nx = x + DX[direction];
    const int ny = y + DY[direction];
    const int nz = z + DZ[direction];
    const ptrdiff_t candidate_index = current_index
                                    + context->neighbour_delta[direction];
    uint8_t next_contacts = 0;
    if (!candidate_is_valid(context->occupied, candidate_index,
                            context->neighbour_delta, ny, nz, contacts,
                            seen_positive_y, seen_positive_z,
                            &next_contacts)) {
      continue;
    }

    const int next_steps = steps + 1;
    context->x[next_steps] = (int8_t) nx;
    context->y[next_steps] = (int8_t) ny;
    context->z[next_steps] = (int8_t) nz;
    context->occupied[(size_t) candidate_index] = 1;
    search_task(context, next_steps, next_contacts,
                seen_positive_y || ny > 0,
                seen_positive_z || nz > 0,
                candidate_index,
                counts);
    context->occupied[(size_t) candidate_index] = 0;
  }
}

static bool is_decimal_text(const char *text) {
  if (*text == '\0') {
    return false;
  }
  for (const unsigned char *cursor = (const unsigned char *) text;
       *cursor != '\0'; ++cursor) {
    if (!isdigit(*cursor)) {
      return false;
    }
  }
  return true;
}

static bool parse_int(const char *text, int *value) {
  char *end = NULL;
  errno = 0;
  if (!is_decimal_text(text)) {
    return false;
  }
  const long parsed = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed < 1 || parsed > INT_MAX) {
    return false;
  }
  *value = (int) parsed;
  return true;
}

static bool parse_size(const char *text, size_t *value) {
  char *end = NULL;
  errno = 0;
  if (!is_decimal_text(text)) {
    return false;
  }
  const uintmax_t parsed = strtoumax(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed == 0
      || parsed > SIZE_MAX) {
    return false;
  }
  *value = (size_t) parsed;
  return true;
}

static bool parse_unsigned(const char *text, unsigned *value) {
  char *end = NULL;
  errno = 0;
  if (!is_decimal_text(text)) {
    return false;
  }
  const uintmax_t parsed = strtoumax(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed == 0
      || parsed > UINT_MAX) {
    return false;
  }
  *value = (unsigned) parsed;
  return true;
}

static bool consume_literal(const char **cursor, const char *literal) {
  const size_t length = strlen(literal);
  if (strncmp(*cursor, literal, length) != 0) {
    return false;
  }
  *cursor += length;
  return true;
}

static bool parse_text_field(const char **cursor, const char delimiter,
                             char *value, const size_t capacity) {
  const char *begin = *cursor;
  const char *end = delimiter == '\0' ? begin + strlen(begin)
                                      : strchr(begin, delimiter);
  if (end == NULL || end == begin) {
    return false;
  }
  const size_t length = (size_t) (end - begin);
  if (length >= capacity) {
    return false;
  }
  memcpy(value, begin, length);
  value[length] = '\0';
  *cursor = delimiter == '\0' ? end : end + 1;
  return true;
}

static bool parse_decimal_field(const char **cursor, const char delimiter,
                                const uintmax_t maximum,
                                uintmax_t *value) {
  const unsigned char *position = (const unsigned char *) *cursor;
  if (!isdigit(*position)) {
    return false;
  }
  uintmax_t parsed = 0;
  do {
    const unsigned digit = (unsigned) (*position - (unsigned char) '0');
    if ((uintmax_t) digit > maximum
        || parsed > (maximum - (uintmax_t) digit) / 10U) {
      return false;
    }
    parsed = 10U * parsed + digit;
    ++position;
  } while (isdigit(*position));
  if ((char) *position != delimiter) {
    return false;
  }
  *value = parsed;
  *cursor = (const char *) position + (delimiter == '\0' ? 0 : 1);
  return true;
}

static bool parse_double_field(const char **cursor, const char delimiter,
                               double *value) {
  const char *begin = *cursor;
  const char *end = delimiter == '\0' ? begin + strlen(begin)
                                      : strchr(begin, delimiter);
  if (end == NULL || end == begin) {
    return false;
  }
  const size_t length = (size_t) (end - begin);
  char token[128];
  if (length >= sizeof(token)) {
    return false;
  }
  memcpy(token, begin, length);
  token[length] = '\0';
  char *parsed_end = NULL;
  errno = 0;
  const double parsed = strtod(token, &parsed_end);
  if (errno != 0 || parsed_end != token + length) {
    return false;
  }
  *value = parsed;
  *cursor = delimiter == '\0' ? end : end + 1;
  return true;
}

static bool parse_fixed_hex_u64(const char *text, uint64_t *value) {
  uint64_t parsed = 0;
  for (size_t i = 0; i < 16; ++i) {
    const unsigned char character = (unsigned char) text[i];
    unsigned digit = 0;
    if (character >= (unsigned char) '0' && character <= (unsigned char) '9') {
      digit = (unsigned) (character - (unsigned char) '0');
    } else if (character >= (unsigned char) 'a'
               && character <= (unsigned char) 'f') {
      digit = 10U + (unsigned) (character - (unsigned char) 'a');
    } else if (character >= (unsigned char) 'A'
               && character <= (unsigned char) 'F') {
      digit = 10U + (unsigned) (character - (unsigned char) 'A');
    } else {
      return false;
    }
    parsed = 16U * parsed + digit;
  }
  *value = parsed;
  return true;
}

static bool flush_stdout_checked(const char *description) {
  if (fflush(stdout) == 0 && !ferror(stdout)) {
    return true;
  }
  const int saved_errno = errno;
  fprintf(stderr, "Cannot write %s to standard output%s%s\n",
          description, saved_errno == 0 ? "" : ": ",
          saved_errno == 0 ? "" : strerror(saved_errno));
  return false;
}

static void usage(const char *program) {
  fprintf(stderr,
          "Usage:\n"
          "  %s compute --max-n N [--threads T] [--split-depth D] [--p3-file FILE]\n"
          "  %s init --state DIR --max-n N [--threads T] [--split-depth D]\n"
          "       [--unit-size U] [--segment-seconds S]\n"
          "  %s next|resume --state DIR\n"
          "  %s status --state DIR\n"
          "  %s verify --state DIR [--p3-file FILE]\n"
          "\n"
          "compute writes raw n,p3(n) rows to standard output. Persistent\n"
          "commands use one consolidated manifest and never overwrite a\n"
          "completed result. An incomplete next/resume stopped by SIGINT or\n"
          "SIGTERM exits 2 after its current recovery unit; failures exit 1.\n"
          "The structural limit is N <= %d; authorization\n"
          "for an official campaign is separate.\n",
          program, program, program, program, program, MAX_STEPS);
}

static bool verify_p3_file(const char *path,
                         const uint64_t counts[MAX_STEPS + 1],
                         const int max_steps) {
  FILE *input = fopen(path, "r");
  if (input == NULL) {
    fprintf(stderr, "Cannot open verification file '%s': %s\n", path, strerror(errno));
    return false;
  }

  bool seen[MAX_STEPS + 1] = {false};
  int largest_reference_index = 0;
  bool ok = true;
  char line[4096];
  unsigned long line_number = 0;
  while (fgets(line, sizeof(line), input) != NULL) {
    ++line_number;
    if (strchr(line, '\n') == NULL) {
      fprintf(stderr, "Overlong or truncated p3 oracle row at line %lu\n",
              line_number);
      ok = false;
      break;
    }
    const char *cursor = line;
    uintmax_t parsed_n = 0;
    uintmax_t parsed_expected = 0;
    int n = 0;
    uint64_t expected = 0;
    char canonical[128];
    int canonical_length = 0;
    if (!parse_decimal_field(&cursor, ' ', INT_MAX, &parsed_n)
        || !parse_decimal_field(&cursor, '\n', UINT64_MAX, &parsed_expected)
        || *cursor != '\0'
        || (canonical_length = snprintf(canonical, sizeof(canonical),
                                        "%" PRIuMAX " %" PRIuMAX "\n",
                                        parsed_n, parsed_expected)) < 0
        || (size_t) canonical_length >= sizeof(canonical)
        || strcmp(line, canonical) != 0) {
      fprintf(stderr, "Malformed p3 oracle row at line %lu\n", line_number);
      ok = false;
      break;
    }
    n = (int) parsed_n;
    expected = (uint64_t) parsed_expected;
    if (n < 1 || n > MAX_STEPS || seen[n]) {
      fprintf(stderr, "Out-of-range or duplicate p3 oracle index at line %lu\n",
              line_number);
      ok = false;
      break;
    }
    seen[n] = true;
    if (n > largest_reference_index) {
      largest_reference_index = n;
    }
    if (n <= max_steps) {
      if (counts[n] != expected) {
        fprintf(stderr,
                "VERIFY FAIL at n=%d: computed=%" PRIu64 ", expected=%" PRIu64 "\n",
                n, counts[n], expected);
        ok = false;
      }
    }
  }
  if (ferror(input)) {
    fprintf(stderr, "Read error in verification file '%s'\n", path);
    ok = false;
  }
  fclose(input);

  if (largest_reference_index == 0) {
    fprintf(stderr, "VERIFY FAIL: reference contains no data rows\n");
    ok = false;
  }

  const int required = largest_reference_index < max_steps
                     ? largest_reference_index : max_steps;
  for (int n = 1; n <= largest_reference_index; ++n) {
    if (!seen[n]) {
      fprintf(stderr, "VERIFY FAIL: reference has no row for n=%d\n", n);
      ok = false;
    }
  }
  if (ok) {
    fprintf(stderr, "VERIFY PASS for n=1..%d against %s\n", required, path);
  }
  return ok;
}

static int run_legacy_compute(int argc, char **argv) {
  int max_steps = 0;
  int threads = 1;
  int split_depth = DEFAULT_SPLIT_DEPTH;
  const char *verification_path = NULL;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--max-n") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], &max_steps)) {
        fprintf(stderr, "Invalid --max-n value\n");
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], &threads)) {
        fprintf(stderr, "Invalid --threads value\n");
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--split-depth") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], &split_depth)) {
        fprintf(stderr, "Invalid --split-depth value\n");
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--p3-file") == 0 && i + 1 < argc) {
      verification_path = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return EXIT_SUCCESS;
    } else {
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (max_steps < 1 || max_steps > MAX_STEPS) {
    fprintf(stderr, "--max-n must be between 1 and %d\n", MAX_STEPS);
    return EXIT_FAILURE;
  }
  if (split_depth > max_steps) {
    split_depth = max_steps;
  }
  if (split_depth < 1) {
    fprintf(stderr, "--split-depth must be positive\n");
    return EXIT_FAILURE;
  }

#ifndef _OPENMP
  if (threads != 1) {
    fprintf(stderr, "This binary was built without OpenMP; use --threads 1\n");
    return EXIT_FAILURE;
  }
#endif

  const int side = 2 * max_steps + 3;
  const int offset = max_steps + 1;
  const size_t grid_size = (size_t) side * (size_t) side * (size_t) side;

  BuildContext build = {0};
  build.max_steps = max_steps;
  build.split_depth = split_depth;
  build.side = side;
  build.offset = offset;
  build.neighbour_delta[0] = (ptrdiff_t) side * side;
  build.neighbour_delta[1] = -build.neighbour_delta[0];
  build.neighbour_delta[2] = side;
  build.neighbour_delta[3] = -side;
  build.neighbour_delta[4] = 1;
  build.neighbour_delta[5] = -1;
  build.grid_size = grid_size;
  build.occupied = calloc(grid_size, sizeof(*build.occupied));
  if (build.occupied == NULL) {
    fprintf(stderr, "Cannot allocate prefix occupancy grid\n");
    return EXIT_FAILURE;
  }

  /* Origin and the canonical first step +x. */
  build.x[0] = 0;
  build.y[0] = 0;
  build.z[0] = 0;
  build.x[1] = 1;
  build.y[1] = 0;
  build.z[1] = 0;
  build.occupied[grid_index(side, offset, 0, 0, 0)] = 1;
  build.occupied[grid_index(side, offset, 1, 0, 0)] = 1;

  const double start = wall_time();
  const ptrdiff_t first_index = (ptrdiff_t) grid_index(side, offset, 1, 0, 0);
  if (!build_prefixes(&build, 1, 0, false, false, first_index)) {
    fprintf(stderr, "Cannot allocate prefix task list\n");
    free(build.tasks.items);
    free(build.occupied);
    return EXIT_FAILURE;
  }
  fprintf(stderr,
          "Generated %zu deterministic tasks at split depth %d; threads=%d\n",
          build.tasks.size, split_depth, threads);

  uint64_t counts[MAX_STEPS + 1] = {0};
  memcpy(counts, build.prefix_counts, sizeof(counts));
  uint64_t completed = 0;
  const uint64_t progress_interval = build.tasks.size >= 20
                                   ? (uint64_t) build.tasks.size / 20U : 0U;
  bool allocation_failed = false;
  bool overflow = false;

#ifdef _OPENMP
#pragma omp parallel num_threads(threads) shared(allocation_failed, overflow, completed, counts)
#endif
  {
    SearchContext search = {0};
    search.max_steps = max_steps;
    search.side = side;
    search.offset = offset;
    memcpy(search.neighbour_delta, build.neighbour_delta,
           sizeof(search.neighbour_delta));
    search.occupied = calloc(grid_size, sizeof(*search.occupied));
    uint64_t local_counts[MAX_STEPS + 1] = {0};
    if (search.occupied == NULL) {
#ifdef _OPENMP
#pragma omp atomic write
#endif
      allocation_failed = true;
    }

#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
    for (size_t task_index = 0; task_index < build.tasks.size; ++task_index) {
      if (search.occupied == NULL) {
        continue;
      }
      const Task *task = &build.tasks.items[task_index];
      for (int step = 0; step <= split_depth; ++step) {
        search.x[step] = task->x[step];
        search.y[step] = task->y[step];
        search.z[step] = task->z[step];
        search.occupied[grid_index(side, offset,
                                   search.x[step], search.y[step], search.z[step])] = 1;
      }

      const ptrdiff_t task_head_index = (ptrdiff_t) grid_index(
          side, offset, search.x[split_depth], search.y[split_depth],
          search.z[split_depth]);
      search_task(&search, split_depth, task->contacts,
                  task->seen_positive_y, task->seen_positive_z,
                  task_head_index,
                  local_counts);

      for (int step = 0; step <= split_depth; ++step) {
        search.occupied[grid_index(side, offset,
                                   search.x[step], search.y[step], search.z[step])] = 0;
      }

      uint64_t done;
#ifdef _OPENMP
#pragma omp atomic capture
#endif
      done = ++completed;
      if (progress_interval != 0 && done % progress_interval == 0) {
#ifdef _OPENMP
#pragma omp critical(progress_output)
#endif
        fprintf(stderr, "Progress: %.1f%%, elapsed %.1f s\n",
                100.0 * (double) done / (double) build.tasks.size,
                wall_time() - start);
      }
    }

    free(search.occupied);
#ifdef _OPENMP
#pragma omp critical(count_merge)
#endif
    {
      for (int n = split_depth; n <= max_steps; ++n) {
        if (UINT64_MAX - counts[n] < local_counts[n]) {
          overflow = true;
        } else {
          counts[n] += local_counts[n];
        }
      }
    }
  }

  free(build.tasks.items);
  free(build.occupied);
  if (allocation_failed) {
    fprintf(stderr, "A worker could not allocate its occupancy grid\n");
    return EXIT_FAILURE;
  }
  if (overflow) {
    fprintf(stderr, "Counter overflow: result is invalid\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr, "Enumeration completed in %.3f seconds\n", wall_time() - start);

  if (verification_path != NULL && !verify_p3_file(verification_path, counts, max_steps)) {
    return EXIT_FAILURE;
  }
  bool output_ok = true;
  for (int n = 1; n <= max_steps; ++n) {
    if (printf("%d %" PRIu64 "\n", n, counts[n]) < 0) {
      output_ok = false;
      break;
    }
  }
  if (!flush_stdout_checked("raw p3 result")) {
    output_ok = false;
  }
  return output_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void handle_stop_signal(const int signal_number) {
  (void) signal_number;
  stop_requested = 1;
}

static bool install_writer_stop_handlers(void) {
  sigset_t blocked_signals;
  sigset_t previous_mask;
  struct sigaction action = {0};
  struct sigaction old_interrupt;
  struct sigaction old_terminate;
  bool interrupt_installed = false;
  bool terminate_installed = false;

  if (sigemptyset(&blocked_signals) != 0
      || sigaddset(&blocked_signals, SIGINT) != 0
      || sigaddset(&blocked_signals, SIGTERM) != 0
      || sigprocmask(SIG_BLOCK, &blocked_signals, &previous_mask) != 0) {
    fprintf(stderr, "Cannot block stop signals: %s\n", strerror(errno));
    return false;
  }

  stop_requested = 0;
  action.sa_handler = handle_stop_signal;
  if (sigemptyset(&action.sa_mask) != 0) {
    fprintf(stderr, "Cannot initialize stop handler mask: %s\n",
            strerror(errno));
    goto fail;
  }
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &action, &old_interrupt) != 0) {
    fprintf(stderr, "Cannot install SIGINT handler: %s\n", strerror(errno));
    goto fail;
  }
  interrupt_installed = true;
  if (sigaction(SIGTERM, &action, &old_terminate) != 0) {
    fprintf(stderr, "Cannot install SIGTERM handler: %s\n", strerror(errno));
    goto fail;
  }
  terminate_installed = true;
  if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) {
    fprintf(stderr, "Cannot restore signal mask: %s\n", strerror(errno));
    goto fail;
  }
  return true;

fail: {
    const int saved_errno = errno;
    if (terminate_installed) {
      (void) sigaction(SIGTERM, &old_terminate, NULL);
    }
    if (interrupt_installed) {
      (void) sigaction(SIGINT, &old_interrupt, NULL);
    }
    (void) sigprocmask(SIG_SETMASK, &previous_mask, NULL);
    errno = saved_errno;
    return false;
  }
}

static bool make_path(char path[PATH_MAX], const char *directory,
                      const char *basename) {
  const int length = snprintf(path, PATH_MAX, "%s/%s", directory, basename);
  if (length < 0 || length >= PATH_MAX) {
    fprintf(stderr, "State path is too long\n");
    return false;
  }
  return true;
}

static bool sync_directory(const char *directory) {
  const int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "Cannot open state directory '%s': %s\n",
            directory, strerror(errno));
    return false;
  }
  const bool ok = fsync(fd) == 0;
  if (!ok) {
    fprintf(stderr, "Cannot sync state directory '%s': %s\n",
            directory, strerror(errno));
  }
  close(fd);
  return ok;
}

static bool sync_parent_directory(const char *path) {
  char parent[PATH_MAX];
  const size_t length = strlen(path);
  if (length == 0 || length >= sizeof(parent)) {
    fprintf(stderr, "State path is too long or empty\n");
    return false;
  }
  memcpy(parent, path, length + 1);
  size_t end = length;
  while (end > 1 && parent[end - 1] == '/') {
    parent[--end] = '\0';
  }
  char *slash = strrchr(parent, '/');
  if (slash == NULL) {
    strcpy(parent, ".");
  } else if (slash == parent) {
    slash[1] = '\0';
  } else {
    *slash = '\0';
  }
  return sync_directory(parent);
}

static uint64_t integrity_update(uint64_t hash, const void *data,
                                 const size_t length) {
  const unsigned char *bytes = data;
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static bool write_hashed(FILE *output, uint64_t *hash,
                         const char *format, ...)
    PRINTF_LIKE(3, 4);

static bool write_hashed(FILE *output, uint64_t *hash,
                         const char *format, ...) {
  char line[4096];
  va_list arguments;
  va_start(arguments, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  const int length = vsnprintf(line, sizeof(line), format, arguments);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(arguments);
  if (length < 0 || (size_t) length >= sizeof(line)) {
    return false;
  }
  if (fwrite(line, 1, (size_t) length, output) != (size_t) length) {
    return false;
  }
  *hash = integrity_update(*hash, line, (size_t) length);
  return true;
}

static void free_build_context(BuildContext *build) {
  free(build->tasks.items);
  free(build->occupied);
  memset(build, 0, sizeof(*build));
}

static bool prepare_build_context(const int max_steps, const int split_depth,
                                  BuildContext *build) {
  memset(build, 0, sizeof(*build));
  const int side = 2 * max_steps + 3;
  const int offset = max_steps + 1;
  const size_t side_size = (size_t) side;
  if (side_size > SIZE_MAX / side_size
      || side_size * side_size > SIZE_MAX / side_size) {
    fprintf(stderr, "Occupancy-grid size overflow\n");
    return false;
  }

  build->max_steps = max_steps;
  build->split_depth = split_depth;
  build->side = side;
  build->offset = offset;
  build->neighbour_delta[0] = (ptrdiff_t) side * side;
  build->neighbour_delta[1] = -build->neighbour_delta[0];
  build->neighbour_delta[2] = side;
  build->neighbour_delta[3] = -side;
  build->neighbour_delta[4] = 1;
  build->neighbour_delta[5] = -1;
  build->grid_size = side_size * side_size * side_size;
  build->occupied = calloc(build->grid_size, sizeof(*build->occupied));
  if (build->occupied == NULL) {
    fprintf(stderr, "Cannot allocate prefix occupancy grid\n");
    return false;
  }

  build->x[0] = 0;
  build->y[0] = 0;
  build->z[0] = 0;
  build->x[1] = 1;
  build->y[1] = 0;
  build->z[1] = 0;
  build->occupied[grid_index(side, offset, 0, 0, 0)] = 1;
  build->occupied[grid_index(side, offset, 1, 0, 0)] = 1;

  const ptrdiff_t first_index =
      (ptrdiff_t) grid_index(side, offset, 1, 0, 0);
  if (!build_prefixes(build, 1, 0, false, false, first_index)) {
    fprintf(stderr, "Cannot allocate prefix task list\n");
    free_build_context(build);
    return false;
  }
  return true;
}

static bool execute_task_range(const BuildContext *build,
                               const size_t task_begin,
                               const size_t task_end,
                               const int threads,
                               uint64_t counts[MAX_STEPS + 1]) {
#ifndef _OPENMP
  (void) threads;
#endif
  if (task_begin > task_end || task_end > build->tasks.size) {
    fprintf(stderr, "Invalid deterministic task range\n");
    return false;
  }
  memset(counts, 0, (MAX_STEPS + 1) * sizeof(*counts));
  bool allocation_failed = false;
  bool overflow = false;

#ifdef _OPENMP
#pragma omp parallel num_threads(threads) shared(allocation_failed, overflow, counts)
#endif
  {
    SearchContext search = {0};
    search.max_steps = build->max_steps;
    search.side = build->side;
    search.offset = build->offset;
    memcpy(search.neighbour_delta, build->neighbour_delta,
           sizeof(search.neighbour_delta));
    search.occupied = calloc(build->grid_size, sizeof(*search.occupied));
    uint64_t local_counts[MAX_STEPS + 1] = {0};
    if (search.occupied == NULL) {
#ifdef _OPENMP
#pragma omp atomic write
#endif
      allocation_failed = true;
    }

#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
    for (size_t task_index = task_begin; task_index < task_end; ++task_index) {
      if (search.occupied == NULL) {
        continue;
      }
      const Task *task = &build->tasks.items[task_index];
      for (int step = 0; step <= build->split_depth; ++step) {
        search.x[step] = task->x[step];
        search.y[step] = task->y[step];
        search.z[step] = task->z[step];
        search.occupied[grid_index(build->side, build->offset,
                                   search.x[step], search.y[step],
                                   search.z[step])] = 1;
      }

      const ptrdiff_t task_head_index = (ptrdiff_t) grid_index(
          build->side, build->offset,
          search.x[build->split_depth], search.y[build->split_depth],
          search.z[build->split_depth]);
      search_task(&search, build->split_depth, task->contacts,
                  task->seen_positive_y, task->seen_positive_z,
                  task_head_index, local_counts);

      for (int step = 0; step <= build->split_depth; ++step) {
        search.occupied[grid_index(build->side, build->offset,
                                   search.x[step], search.y[step],
                                   search.z[step])] = 0;
      }
    }

    free(search.occupied);
#ifdef _OPENMP
#pragma omp critical(persistent_count_merge)
#endif
    {
      for (int n = build->split_depth; n <= build->max_steps; ++n) {
        if (UINT64_MAX - counts[n] < local_counts[n]) {
          overflow = true;
        } else {
          counts[n] += local_counts[n];
        }
      }
    }
  }

  if (allocation_failed) {
    fprintf(stderr, "A worker could not allocate its occupancy grid\n");
    return false;
  }
  if (overflow) {
    fprintf(stderr, "Counter overflow in deterministic task range\n");
    return false;
  }
  return true;
}

static void free_manifest(Manifest *manifest) {
  free(manifest->units);
  memset(manifest, 0, sizeof(*manifest));
}

static bool manifest_config_equal(const Manifest *left,
                                  const Manifest *right) {
  return strcmp(left->source_sha, right->source_sha) == 0
      && strcmp(left->makefile_sha, right->makefile_sha) == 0
      && left->max_steps == right->max_steps
      && left->split_depth == right->split_depth
      && left->threads == right->threads
      && left->unit_size == right->unit_size
      && left->task_count == right->task_count
      && left->unit_count == right->unit_count
      && left->segment_seconds == right->segment_seconds;
}

static bool manifest_prefix_equal(const Manifest *left,
                                  const Manifest *right) {
  if (right->completed_units < left->completed_units) {
    return false;
  }
  for (size_t i = 0; i < left->completed_units; ++i) {
    const UnitRecord *a = &left->units[i];
    const UnitRecord *b = &right->units[i];
    if (a->task_begin != b->task_begin || a->task_end != b->task_end
        || a->segment_id != b->segment_id
        || a->elapsed_seconds != b->elapsed_seconds
        || memcmp(a->counts, b->counts, sizeof(a->counts)) != 0) {
      return false;
    }
  }
  return true;
}

static bool write_manifest_stream(FILE *output, const Manifest *manifest) {
  uint64_t hash = UINT64_C(14695981039346656037);
  if (!write_hashed(output, &hash, "%s\n", MANIFEST_MAGIC)
      || !write_hashed(output, &hash,
                       "CONFIG\t%s\t%s\t%s\t%d\t%d\t%d\t%zu\t%zu\t%zu\t%u\n",
                       PROGRAM_ID, manifest->source_sha,
                       manifest->makefile_sha, manifest->max_steps,
                       manifest->split_depth, manifest->threads,
                       manifest->unit_size, manifest->task_count,
                       manifest->unit_count, manifest->segment_seconds)
      || !write_hashed(output, &hash,
                       "STATE\t%" PRIu64 "\t%u\t%d\t%.6f\t%zu\n",
                       manifest->generation, manifest->segment_id,
                       manifest->segment_open ? 1 : 0,
                       manifest->segment_elapsed,
                       manifest->completed_units)) {
    return false;
  }

  for (size_t unit_id = 0; unit_id < manifest->completed_units; ++unit_id) {
    const UnitRecord *unit = &manifest->units[unit_id];
    if (!write_hashed(output, &hash, "UNIT\t%zu\t%zu\t%zu\t%u\t%.6f\t",
                      unit_id, unit->task_begin, unit->task_end,
                      unit->segment_id, unit->elapsed_seconds)) {
      return false;
    }
    for (int n = 1; n <= manifest->max_steps; ++n) {
      if (!write_hashed(output, &hash, "%s%" PRIu64,
                        n == 1 ? "" : ",", unit->counts[n])) {
        return false;
      }
    }
    if (!write_hashed(output, &hash, "\n")) {
      return false;
    }
  }
  return fprintf(output, "FNV1A64\t%016" PRIx64 "\n", hash) > 0;
}

static bool write_manifest_atomic(const char *directory,
                                  const Manifest *manifest) {
  char temporary_path[PATH_MAX];
  char manifest_path[PATH_MAX];
  if (!make_path(temporary_path, directory, "manifest.tmp")
      || !make_path(manifest_path, directory, "manifest.tsv")) {
    return false;
  }

  const int fd = open(temporary_path,
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                      0644);
  if (fd < 0) {
    fprintf(stderr, "Cannot create '%s': %s\n",
            temporary_path, strerror(errno));
    return false;
  }
  FILE *output = fdopen(fd, "w");
  if (output == NULL) {
    fprintf(stderr, "Cannot open manifest stream: %s\n", strerror(errno));
    close(fd);
    return false;
  }

  bool ok = write_manifest_stream(output, manifest);
  if (ok && fflush(output) != 0) {
    ok = false;
  }
  if (ok && fsync(fd) != 0) {
    ok = false;
  }
  if (fclose(output) != 0) {
    ok = false;
  }
  if (!ok) {
    fprintf(stderr, "Cannot durably write '%s': %s\n",
            temporary_path, strerror(errno));
    return false;
  }
  if (rename(temporary_path, manifest_path) != 0) {
    fprintf(stderr, "Cannot install manifest '%s': %s\n",
            manifest_path, strerror(errno));
    return false;
  }
  return sync_directory(directory);
}

static bool parse_counts(const char *cursor, const int max_steps,
                         uint64_t counts[MAX_STEPS + 1]) {
  memset(counts, 0, (MAX_STEPS + 1) * sizeof(*counts));
  for (int n = 1; n <= max_steps; ++n) {
    uintmax_t value = 0;
    const char delimiter = n == max_steps ? '\0' : ',';
    if (!parse_decimal_field(&cursor, delimiter, UINT64_MAX, &value)) {
      return false;
    }
    counts[n] = (uint64_t) value;
  }
  return *cursor == '\0';
}

static bool append_format(char *buffer, const size_t capacity, size_t *used,
                          const char *format, ...)
    PRINTF_LIKE(4, 5);

static bool append_format(char *buffer, const size_t capacity, size_t *used,
                          const char *format, ...) {
  if (*used >= capacity) {
    return false;
  }
  va_list arguments;
  va_start(arguments, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  const int length = vsnprintf(buffer + *used, capacity - *used,
                               format, arguments);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(arguments);
  if (length < 0 || (size_t) length >= capacity - *used) {
    return false;
  }
  *used += (size_t) length;
  return true;
}

static bool unit_line_is_canonical(const char *line, const size_t unit_id,
                                   const UnitRecord *unit,
                                   const int max_steps) {
  char canonical[4096];
  size_t used = 0;
  if (!append_format(canonical, sizeof(canonical), &used,
                     "UNIT\t%zu\t%zu\t%zu\t%u\t%.6f\t",
                     unit_id, unit->task_begin, unit->task_end,
                     unit->segment_id, unit->elapsed_seconds)) {
    return false;
  }
  for (int n = 1; n <= max_steps; ++n) {
    if (!append_format(canonical, sizeof(canonical), &used,
                       "%s%" PRIu64, n == 1 ? "" : ",",
                       unit->counts[n])) {
      return false;
    }
  }
  return strcmp(line, canonical) == 0;
}

static bool manifest_state_is_reachable(const Manifest *manifest) {
  if (manifest->completed_units == manifest->unit_count
      && manifest->segment_open) {
    return false;
  }
  const bool complete = manifest->completed_units == manifest->unit_count;
  uint64_t expected_generation = 1U + (uint64_t) manifest->completed_units;
  const uint64_t segment_increment = 2U * (uint64_t) manifest->segment_id;
  if (UINT64_MAX - expected_generation < segment_increment) {
    return false;
  }
  expected_generation += segment_increment;
  if (manifest->segment_open) {
    --expected_generation;
  }
  if (complete) {
    --expected_generation;
  }
  if (manifest->generation != expected_generation) {
    return false;
  }

  if (manifest->segment_id == 0) {
    return manifest->completed_units == 0 && !manifest->segment_open
        && manifest->segment_elapsed == 0.0;
  }
  if (manifest->completed_units == 0) {
    return manifest->segment_id == 1 && manifest->segment_open
        && manifest->segment_elapsed == 0.0;
  }

  unsigned previous_segment = 0;
  size_t current_segment_units = 0;
  double represented_elapsed = 0.0;
  for (size_t unit_id = 0; unit_id < manifest->completed_units; ++unit_id) {
    const unsigned segment = manifest->units[unit_id].segment_id;
    if ((unit_id == 0 && segment != 1)
        || (unit_id > 0
            && (segment < previous_segment
                || segment - previous_segment > 1U))) {
      return false;
    }
    previous_segment = segment;
    if (segment == manifest->segment_id) {
      represented_elapsed += manifest->units[unit_id].elapsed_seconds;
      ++current_segment_units;
    }
  }
  if (previous_segment > manifest->segment_id
      || manifest->segment_id - previous_segment > 1U
      || (!manifest->segment_open && previous_segment != manifest->segment_id)
      || (previous_segment < manifest->segment_id
          && (!manifest->segment_open || current_segment_units != 0))) {
    return false;
  }
  const double tolerance = 0.000000500001
                         * (double) (current_segment_units + 1U);
  return fabs(manifest->segment_elapsed - represented_elapsed) <= tolerance;
}

static bool valid_recorded_sha(const char *text) {
  if (strlen(text) != 64) {
    return false;
  }
  for (size_t i = 0; i < 64; ++i) {
    if (!isxdigit((unsigned char) text[i])) {
      return false;
    }
  }
  return true;
}

static bool load_file_bytes(const char *path, char **bytes_out,
                            size_t *length_out) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  struct stat status;
  if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)
      || status.st_size <= 0
      || (uintmax_t) status.st_size > MAX_MANIFEST_BYTES) {
    close(fd);
    errno = EINVAL;
    return false;
  }
  const size_t length = (size_t) status.st_size;
  char *bytes = malloc(length + 1);
  if (bytes == NULL) {
    close(fd);
    return false;
  }
  size_t used = 0;
  while (used < length) {
    const ssize_t amount = read(fd, bytes + used, length - used);
    if (amount < 0 && errno == EINTR) {
      continue;
    }
    if (amount <= 0) {
      free(bytes);
      close(fd);
      errno = amount == 0 ? EIO : errno;
      return false;
    }
    used += (size_t) amount;
  }
  close(fd);
  bytes[length] = '\0';
  *bytes_out = bytes;
  *length_out = length;
  return true;
}

static bool load_manifest_path(const char *path, Manifest *manifest,
                               const bool report_errors) {
  memset(manifest, 0, sizeof(*manifest));
  char *bytes = NULL;
  size_t length = 0;
  if (!load_file_bytes(path, &bytes, &length)) {
    if (report_errors) {
      fprintf(stderr, "Cannot read manifest '%s': %s\n", path, strerror(errno));
    }
    return false;
  }
  if (bytes[length - 1] != '\n' || memchr(bytes, '\0', length) != NULL) {
    if (report_errors) {
      fprintf(stderr,
              "Manifest is truncated or contains an embedded NUL in '%s'\n",
              path);
    }
    free(bytes);
    return false;
  }

  const char marker[] = "FNV1A64\t";
  char *checksum_line = NULL;
  for (char *position = bytes; (position = strstr(position, marker)) != NULL;
       ++position) {
    if (position == bytes || position[-1] == '\n') {
      checksum_line = position;
    }
  }
  bool ok = checksum_line != NULL;
  uint64_t stored_hash = 0;
  size_t checksum_length = 0;
  if (ok) {
    checksum_length = strlen(checksum_line);
    ok = checksum_length == sizeof("FNV1A64\t") - 1 + 16 + 1
      && checksum_line[checksum_length - 1] == '\n';
    for (size_t i = sizeof("FNV1A64\t") - 1;
         ok && i < checksum_length - 1; ++i) {
      ok = isxdigit((unsigned char) checksum_line[i]) != 0;
    }
    if (ok) {
      ok = parse_fixed_hex_u64(checksum_line + sizeof("FNV1A64\t") - 1,
                               &stored_hash);
    }
  }
  const size_t payload_length = ok ? (size_t) (checksum_line - bytes) : 0;
  if (ok) {
    const uint64_t actual_hash = integrity_update(
        UINT64_C(14695981039346656037), bytes, payload_length);
    ok = payload_length + checksum_length == length
      && stored_hash == actual_hash;
    char canonical_checksum[sizeof("FNV1A64\t") - 1 + 16 + 2];
    const int canonical_length = snprintf(canonical_checksum,
                                           sizeof(canonical_checksum),
                                           "FNV1A64\t%016" PRIx64 "\n",
                                           stored_hash);
    ok = ok && canonical_length > 0
      && (size_t) canonical_length < sizeof(canonical_checksum)
      && strcmp(checksum_line, canonical_checksum) == 0;
  }
  if (ok) {
    ok = payload_length > 0 && bytes[payload_length - 1] == '\n'
      && memchr(bytes, '\0', payload_length) == NULL
      && memchr(bytes, '\r', payload_length) == NULL;
    for (size_t i = 1; ok && i < payload_length; ++i) {
      if (bytes[i - 1] == '\n' && bytes[i] == '\n') {
        ok = false;
      }
    }
  }
  if (!ok) {
    if (report_errors) {
      fprintf(stderr, "Manifest checksum/truncation failure in '%s'\n", path);
    }
    free(bytes);
    return false;
  }
  *checksum_line = '\0';

  char *save = NULL;
  const char *line = strtok_r(bytes, "\n", &save);
  if (line == NULL || strcmp(line, MANIFEST_MAGIC) != 0) {
    ok = false;
  }
  line = ok ? strtok_r(NULL, "\n", &save) : NULL;
  char program_identity[64] = {0};
  uintmax_t parsed_max_steps = 0;
  uintmax_t parsed_split_depth = 0;
  uintmax_t parsed_threads = 0;
  uintmax_t parsed_unit_size = 0;
  uintmax_t parsed_task_count = 0;
  uintmax_t parsed_unit_count = 0;
  uintmax_t parsed_segment_seconds = 0;
  const char *cursor = line;
  if (line == NULL
      || !consume_literal(&cursor, "CONFIG\t")
      || !parse_text_field(&cursor, '\t', program_identity,
                           sizeof(program_identity))
      || !parse_text_field(&cursor, '\t', manifest->source_sha,
                           sizeof(manifest->source_sha))
      || !parse_text_field(&cursor, '\t', manifest->makefile_sha,
                           sizeof(manifest->makefile_sha))
      || !parse_decimal_field(&cursor, '\t', INT_MAX, &parsed_max_steps)
      || !parse_decimal_field(&cursor, '\t', INT_MAX, &parsed_split_depth)
      || !parse_decimal_field(&cursor, '\t', INT_MAX, &parsed_threads)
      || !parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_unit_size)
      || !parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_task_count)
      || !parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_unit_count)
      || !parse_decimal_field(&cursor, '\0', UINT_MAX,
                              &parsed_segment_seconds)
      || *cursor != '\0' || strcmp(program_identity, PROGRAM_ID) != 0) {
    ok = false;
  } else {
    manifest->max_steps = (int) parsed_max_steps;
    manifest->split_depth = (int) parsed_split_depth;
    manifest->threads = (int) parsed_threads;
    manifest->unit_size = (size_t) parsed_unit_size;
    manifest->task_count = (size_t) parsed_task_count;
    manifest->unit_count = (size_t) parsed_unit_count;
    manifest->segment_seconds = (unsigned) parsed_segment_seconds;
  }
  if (ok) {
    char canonical[4096];
    const int length_written = snprintf(
        canonical, sizeof(canonical),
        "CONFIG\t%s\t%s\t%s\t%d\t%d\t%d\t%zu\t%zu\t%zu\t%u",
        program_identity, manifest->source_sha, manifest->makefile_sha,
        manifest->max_steps, manifest->split_depth, manifest->threads,
        manifest->unit_size, manifest->task_count, manifest->unit_count,
        manifest->segment_seconds);
    ok = length_written > 0 && (size_t) length_written < sizeof(canonical)
      && strcmp(line, canonical) == 0;
  }
  line = ok ? strtok_r(NULL, "\n", &save) : NULL;
  uintmax_t parsed_generation = 0;
  uintmax_t parsed_segment_id = 0;
  uintmax_t parsed_open_value = 0;
  uintmax_t parsed_completed_units = 0;
  int open_value = 0;
  cursor = line;
  if (line == NULL
      || !consume_literal(&cursor, "STATE\t")
      || !parse_decimal_field(&cursor, '\t', UINT64_MAX,
                              &parsed_generation)
      || !parse_decimal_field(&cursor, '\t', UINT_MAX,
                              &parsed_segment_id)
      || !parse_decimal_field(&cursor, '\t', INT_MAX,
                              &parsed_open_value)
      || !parse_double_field(&cursor, '\t', &manifest->segment_elapsed)
      || !parse_decimal_field(&cursor, '\0', SIZE_MAX,
                              &parsed_completed_units)
      || *cursor != '\0'
      || (parsed_open_value != 0 && parsed_open_value != 1)) {
    ok = false;
  } else {
    manifest->generation = (uint64_t) parsed_generation;
    manifest->segment_id = (unsigned) parsed_segment_id;
    open_value = (int) parsed_open_value;
    manifest->completed_units = (size_t) parsed_completed_units;
  }
  manifest->segment_open = open_value != 0;
  if (ok) {
    char canonical[4096];
    const int length_written = snprintf(
        canonical, sizeof(canonical),
        "STATE\t%" PRIu64 "\t%u\t%d\t%.6f\t%zu",
        manifest->generation, manifest->segment_id, open_value,
        manifest->segment_elapsed, manifest->completed_units);
    ok = length_written > 0 && (size_t) length_written < sizeof(canonical)
      && strcmp(line, canonical) == 0;
  }

  if (ok && (manifest->max_steps < 1 || manifest->max_steps > MAX_STEPS
             || !valid_recorded_sha(manifest->source_sha)
             || !valid_recorded_sha(manifest->makefile_sha)
             || manifest->split_depth < 1
             || manifest->split_depth > manifest->max_steps
             || manifest->split_depth > MAX_SPLIT_DEPTH
             || manifest->threads < 1 || manifest->threads > MAX_THREADS
             || manifest->unit_size == 0
             || manifest->task_count == 0 || manifest->unit_count == 0
             || manifest->unit_count > MAX_RECOVERY_UNITS
             || manifest->unit_size > SIZE_MAX / manifest->unit_count
             || manifest->completed_units > manifest->unit_count
             || manifest->segment_seconds == 0
             || manifest->segment_seconds > 5400
             || manifest->generation == 0
             || manifest->generation < manifest->completed_units + 1
             || !isfinite(manifest->segment_elapsed)
             || manifest->segment_elapsed < 0.0
             || signbit(manifest->segment_elapsed))) {
    ok = false;
  }

  if (ok) {
    manifest->units = calloc(manifest->unit_count, sizeof(*manifest->units));
    if (manifest->units == NULL) {
      ok = false;
    }
  }
  for (size_t expected_id = 0; ok && expected_id < manifest->completed_units;
       ++expected_id) {
    line = strtok_r(NULL, "\n", &save);
    uintmax_t parsed_unit_id = 0;
    uintmax_t parsed_task_begin = 0;
    uintmax_t parsed_task_end = 0;
    uintmax_t parsed_unit_segment = 0;
    size_t unit_id = 0;
    UnitRecord *unit = &manifest->units[expected_id];
    cursor = line;
    const bool unit_fields_ok = line != NULL
        && consume_literal(&cursor, "UNIT\t")
        && parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_unit_id)
        && parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_task_begin)
        && parse_decimal_field(&cursor, '\t', SIZE_MAX, &parsed_task_end)
        && parse_decimal_field(&cursor, '\t', UINT_MAX,
                               &parsed_unit_segment)
        && parse_double_field(&cursor, '\t', &unit->elapsed_seconds);
    if (unit_fields_ok) {
      unit_id = (size_t) parsed_unit_id;
      unit->task_begin = (size_t) parsed_task_begin;
      unit->task_end = (size_t) parsed_task_end;
      unit->segment_id = (unsigned) parsed_unit_segment;
    }
    if (!unit_fields_ok || unit_id != expected_id || unit->segment_id == 0
        || unit->segment_id > manifest->segment_id
        || !isfinite(unit->elapsed_seconds) || unit->elapsed_seconds < 0.0
        || signbit(unit->elapsed_seconds)
        || !parse_counts(cursor, manifest->max_steps,
                         unit->counts)
        || !unit_line_is_canonical(line, unit_id, unit,
                                   manifest->max_steps)) {
      ok = false;
      break;
    }
    for (int n = 1; n < manifest->split_depth; ++n) {
      if (unit->counts[n] != 0) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      break;
    }
    const size_t expected_begin = expected_id * manifest->unit_size;
    size_t expected_end = expected_begin + manifest->unit_size;
    if (expected_end > manifest->task_count) {
      expected_end = manifest->task_count;
    }
    if (unit->task_begin != expected_begin || unit->task_end != expected_end
        || (expected_id > 0
            && unit->segment_id < manifest->units[expected_id - 1].segment_id)) {
      ok = false;
    }
  }
  if (ok && strtok_r(NULL, "\n", &save) != NULL) {
    ok = false;
  }
  if (ok && manifest->unit_count
            != (manifest->task_count - 1) / manifest->unit_size + 1) {
    ok = false;
  }
  if (ok && !manifest_state_is_reachable(manifest)) {
    ok = false;
  }
  if (!ok && report_errors) {
    fprintf(stderr, "Manifest semantic validation failure in '%s'\n", path);
  }
  free(bytes);
  if (!ok) {
    free_manifest(manifest);
  }
  return ok;
}

static bool load_manifest(const char *directory, Manifest *manifest,
                          const bool allow_recovery) {
  char manifest_path[PATH_MAX];
  char temporary_path[PATH_MAX];
  if (!make_path(manifest_path, directory, "manifest.tsv")
      || !make_path(temporary_path, directory, "manifest.tmp")) {
    return false;
  }
  Manifest current;
  bool recovered_initial = false;
  if (!load_manifest_path(manifest_path, &current, false)) {
    struct stat manifest_status;
    if (lstat(manifest_path, &manifest_status) == 0 || errno != ENOENT) {
      (void) load_manifest_path(manifest_path, &current, true);
      return false;
    }
    if (!allow_recovery) {
      fprintf(stderr, "Cannot read manifest '%s': %s\n",
              manifest_path, strerror(ENOENT));
      return false;
    }
    struct stat temporary_status;
    Manifest temporary;
    const bool temporary_valid = lstat(temporary_path, &temporary_status) == 0
                              && S_ISREG(temporary_status.st_mode)
                              && load_manifest_path(temporary_path,
                                                    &temporary, false);
    if (!temporary_valid || temporary.generation != 1
        || temporary.segment_id != 0 || temporary.segment_open
        || temporary.completed_units != 0
        || strcmp(temporary.source_sha, A049230_SOURCE_SHA) != 0
        || strcmp(temporary.makefile_sha, A049230_MAKEFILE_SHA) != 0) {
      if (temporary_valid) {
        free_manifest(&temporary);
      }
      fprintf(stderr, "No authoritative or recoverable initial manifest\n");
      return false;
    }
    if (rename(temporary_path, manifest_path) != 0
        || !sync_directory(directory)) {
      fprintf(stderr, "Cannot recover initial manifest: %s\n",
              strerror(errno));
      free_manifest(&temporary);
      return false;
    }
    current = temporary;
    recovered_initial = true;
    fprintf(stderr, "Recovered validated initial manifest\n");
  }

  if (strcmp(current.source_sha, A049230_SOURCE_SHA) != 0
      || strcmp(current.makefile_sha, A049230_MAKEFILE_SHA) != 0) {
    fprintf(stderr,
            "Manifest program identity does not match this binary\n");
    free_manifest(&current);
    return false;
  }

  struct stat temporary_status;
  if (allow_recovery && !recovered_initial
      && lstat(temporary_path, &temporary_status) == 0) {
    Manifest temporary;
    const bool temporary_valid = S_ISREG(temporary_status.st_mode)
                              && load_manifest_path(temporary_path,
                                                    &temporary, false);
    bool promote = false;
    if (temporary_valid) {
      promote = manifest_config_equal(&current, &temporary)
             && temporary.generation == current.generation + 1
             && temporary.completed_units >= current.completed_units
             && temporary.completed_units <= current.completed_units + 1
             && temporary.segment_id >= current.segment_id
             && temporary.segment_id - current.segment_id <= 1U
             && manifest_prefix_equal(&current, &temporary);
    }
    if (promote) {
      if (rename(temporary_path, manifest_path) != 0
          || !sync_directory(directory)) {
        fprintf(stderr, "Cannot recover durable manifest update: %s\n",
                strerror(errno));
        free_manifest(&temporary);
        free_manifest(&current);
        return false;
      }
      fprintf(stderr, "Recovered one validated manifest update\n");
      free_manifest(&current);
      current = temporary;
    } else {
      if (temporary_valid) {
        free_manifest(&temporary);
      }
      if (unlink(temporary_path) != 0 || !sync_directory(directory)) {
        fprintf(stderr, "Cannot remove non-authoritative manifest temporary\n");
        free_manifest(&current);
        return false;
      }
      fprintf(stderr, "Discarded non-authoritative manifest temporary\n");
    }
  } else if (allow_recovery && !recovered_initial && errno != ENOENT) {
    fprintf(stderr, "Cannot inspect manifest temporary: %s\n", strerror(errno));
    free_manifest(&current);
    return false;
  }
  *manifest = current;
  return true;
}

static bool aggregate_counts(const BuildContext *build,
                             const Manifest *manifest,
                             uint64_t counts[MAX_STEPS + 1]) {
  memcpy(counts, build->prefix_counts,
         (MAX_STEPS + 1) * sizeof(*counts));
  for (size_t unit_id = 0; unit_id < manifest->completed_units; ++unit_id) {
    for (int n = build->split_depth; n <= build->max_steps; ++n) {
      if (UINT64_MAX - counts[n] < manifest->units[unit_id].counts[n]) {
        fprintf(stderr, "Counter overflow while aggregating manifest\n");
        return false;
      }
      counts[n] += manifest->units[unit_id].counts[n];
    }
  }
  return true;
}

static bool emit_event(const char *event, const char *format, ...)
    PRINTF_LIKE(2, 3);

static bool emit_event(const char *event, const char *format, ...) {
  struct timespec now;
  struct tm utc;
  bool ok = clock_gettime(CLOCK_REALTIME, &now) == 0
         && gmtime_r(&now.tv_sec, &utc) != NULL;
  char timestamp[32];
  if (ok && strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ",
                     &utc) == 0) {
    ok = false;
  }
  if (ok && fprintf(stderr, "%s\t%s\t", timestamp, event) < 0) {
    ok = false;
  }
  if (ok) {
    va_list arguments;
    va_start(arguments, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    if (vfprintf(stderr, format, arguments) < 0) {
      ok = false;
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(arguments);
  }
  if (ok && fputc('\n', stderr) == EOF) {
    ok = false;
  }
  if (ok && fflush(stderr) != 0) {
    ok = false;
  }
  if (!ok) {
    fprintf(stderr, "Cannot emit live telemetry\n");
  }
  return ok;
}

static int acquire_lock(const char *directory) {
  const int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "Cannot open campaign lock: %s\n", strerror(errno));
    return -1;
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    fprintf(stderr, "Campaign is already locked by another process\n");
    close(fd);
    return -1;
  }
  return fd;
}

static bool release_lock(const int fd) {
  bool ok = true;
  if (flock(fd, LOCK_UN) != 0) {
    ok = false;
  }
  if (close(fd) != 0) {
    ok = false;
  }
  if (!ok) {
    fprintf(stderr, "Cannot cleanly release campaign lock\n");
  }
  return ok;
}

static bool parse_results_file(const char *path, const int max_steps,
                               uint64_t counts[MAX_STEPS + 1]) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "Cannot open results '%s': %s\n", path, strerror(errno));
    return false;
  }
  FILE *input = fdopen(fd, "r");
  if (input == NULL) {
    close(fd);
    return false;
  }
  memset(counts, 0, (MAX_STEPS + 1) * sizeof(*counts));
  bool ok = true;
  char line[256];
  for (int expected_n = 1; expected_n <= max_steps; ++expected_n) {
    if (fgets(line, sizeof(line), input) == NULL) {
      ok = false;
      break;
    }
    if (strchr(line, '\n') == NULL) {
      ok = false;
      break;
    }
    const char *cursor = line;
    uintmax_t parsed_n = 0;
    uintmax_t parsed_value = 0;
    int n = 0;
    uint64_t value = 0;
    char canonical[128];
    int canonical_length = 0;
    if (!parse_decimal_field(&cursor, ' ', INT_MAX, &parsed_n)
        || !parse_decimal_field(&cursor, '\n', UINT64_MAX, &parsed_value)
        || *cursor != '\0'
        || (canonical_length = snprintf(canonical, sizeof(canonical),
                                        "%" PRIuMAX " %" PRIuMAX "\n",
                                        parsed_n, parsed_value)) <= 0
        || (size_t) canonical_length >= sizeof(canonical)
        || strcmp(line, canonical) != 0) {
      ok = false;
      break;
    }
    n = (int) parsed_n;
    value = (uint64_t) parsed_value;
    if (n != expected_n) {
      ok = false;
      break;
    }
    counts[n] = value;
  }
  if (ok && fgets(line, sizeof(line), input) != NULL) {
    ok = false;
  }
  if (ferror(input)) {
    ok = false;
  }
  fclose(input);
  if (!ok) {
    fprintf(stderr, "Results file is truncated, duplicated, or malformed\n");
  }
  return ok;
}

static bool results_equal(const uint64_t left[MAX_STEPS + 1],
                          const uint64_t right[MAX_STEPS + 1],
                          const int max_steps) {
  for (int n = 1; n <= max_steps; ++n) {
    if (left[n] != right[n]) {
      return false;
    }
  }
  return true;
}

static bool write_results_atomic(const char *directory,
                                 const uint64_t counts[MAX_STEPS + 1],
                                 const int max_steps) {
  char result_path[PATH_MAX];
  char temporary_path[PATH_MAX];
  if (!make_path(result_path, directory, "results.tsv")
      || !make_path(temporary_path, directory, "results.tmp")) {
    return false;
  }

  struct stat result_status;
  if (lstat(result_path, &result_status) == 0) {
    uint64_t existing[MAX_STEPS + 1];
    if (!S_ISREG(result_status.st_mode)
        || !parse_results_file(result_path, max_steps, existing)) {
      return false;
    }
    if (!results_equal(existing, counts, max_steps)) {
      fprintf(stderr, "Existing completed result disagrees; refusing clobber\n");
      return false;
    }
    return true;
  } else if (errno != ENOENT) {
    fprintf(stderr, "Cannot inspect results path: %s\n", strerror(errno));
    return false;
  }

  struct stat temporary_status;
  if (lstat(temporary_path, &temporary_status) == 0) {
    uint64_t temporary[MAX_STEPS + 1];
    if (!S_ISREG(temporary_status.st_mode)) {
      fprintf(stderr, "Non-authoritative results temporary is invalid\n");
      return false;
    }
    const bool temporary_matches =
        parse_results_file(temporary_path, max_steps, temporary)
        && results_equal(temporary, counts, max_steps);
    if (temporary_matches) {
      if (rename(temporary_path, result_path) != 0) {
        fprintf(stderr, "Cannot recover results temporary: %s\n",
                strerror(errno));
        return false;
      }
      return sync_directory(directory);
    }
    if (unlink(temporary_path) != 0 || !sync_directory(directory)) {
      fprintf(stderr, "Cannot discard invalid results temporary: %s\n",
              strerror(errno));
      return false;
    }
    fprintf(stderr, "Discarded non-authoritative results temporary\n");
  } else if (errno != ENOENT) {
    fprintf(stderr, "Cannot inspect results temporary: %s\n", strerror(errno));
    return false;
  }

  const int fd = open(temporary_path,
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                      0644);
  if (fd < 0) {
    fprintf(stderr, "Cannot create results temporary: %s\n", strerror(errno));
    return false;
  }
  FILE *output = fdopen(fd, "w");
  if (output == NULL) {
    close(fd);
    return false;
  }
  bool ok = true;
  for (int n = 1; n <= max_steps; ++n) {
    if (fprintf(output, "%d %" PRIu64 "\n", n, counts[n]) < 0) {
      ok = false;
      break;
    }
  }
  if (ok && fflush(output) != 0) {
    ok = false;
  }
  if (ok && fsync(fd) != 0) {
    ok = false;
  }
  if (fclose(output) != 0) {
    ok = false;
  }
  if (!ok || rename(temporary_path, result_path) != 0
      || !sync_directory(directory)) {
    fprintf(stderr, "Cannot install durable results: %s\n", strerror(errno));
    return false;
  }
  return true;
}

static bool initialize_campaign(const Options *options) {
  BuildContext build;
  if (!prepare_build_context(options->max_steps, options->split_depth, &build)) {
    return false;
  }
  const size_t unit_count =
      (build.tasks.size - 1) / options->unit_size + 1;
  if (unit_count > MAX_RECOVERY_UNITS) {
    fprintf(stderr,
            "Configuration needs %zu recovery units; allowed maximum is %d\n",
            unit_count, MAX_RECOVERY_UNITS);
    free_build_context(&build);
    return false;
  }

  Manifest manifest = {0};
  if (snprintf(manifest.source_sha, sizeof(manifest.source_sha), "%s",
               A049230_SOURCE_SHA) >= (int) sizeof(manifest.source_sha)
      || snprintf(manifest.makefile_sha, sizeof(manifest.makefile_sha), "%s",
                  A049230_MAKEFILE_SHA) >= (int) sizeof(manifest.makefile_sha)
      || !valid_recorded_sha(manifest.source_sha)
      || !valid_recorded_sha(manifest.makefile_sha)) {
    fprintf(stderr, "Compiled provenance identity is invalid\n");
    free_build_context(&build);
    return false;
  }
  manifest.max_steps = options->max_steps;
  manifest.split_depth = options->split_depth;
  manifest.threads = options->threads;
  manifest.unit_size = options->unit_size;
  manifest.task_count = build.tasks.size;
  manifest.unit_count = unit_count;
  manifest.segment_seconds = options->segment_seconds;
  manifest.generation = 1;
  manifest.units = calloc(unit_count, sizeof(*manifest.units));
  free_build_context(&build);
  if (manifest.units == NULL) {
    fprintf(stderr, "Cannot allocate campaign manifest\n");
    return false;
  }

  if (mkdir(options->state_directory, 0755) != 0) {
    fprintf(stderr, "Refusing to clobber state directory '%s': %s\n",
            options->state_directory, strerror(errno));
    free_manifest(&manifest);
    return false;
  }
  if (!sync_parent_directory(options->state_directory)) {
    if (rmdir(options->state_directory) == 0) {
      (void) sync_parent_directory(options->state_directory);
    }
    free_manifest(&manifest);
    return false;
  }

  bool ok = write_manifest_atomic(options->state_directory, &manifest);
  if (!ok) {
    char manifest_path[PATH_MAX];
    char temporary_path[PATH_MAX];
    struct stat status;
    if (make_path(manifest_path, options->state_directory, "manifest.tsv")
        && make_path(temporary_path, options->state_directory, "manifest.tmp")
        && lstat(manifest_path, &status) != 0 && errno == ENOENT) {
      if (lstat(temporary_path, &status) == 0 && S_ISREG(status.st_mode)) {
        (void) unlink(temporary_path);
      }
      if (rmdir(options->state_directory) == 0) {
        (void) sync_parent_directory(options->state_directory);
      }
    }
    free_manifest(&manifest);
    return false;
  }
  if (!emit_event("INIT",
                  "program=%s max_n=%d split=%d threads=%d "
                  "tasks=%zu units=%zu unit_size=%zu segment_s=%u",
                  PROGRAM_ID, manifest.max_steps,
                  manifest.split_depth, manifest.threads,
                  manifest.task_count, manifest.unit_count,
                  manifest.unit_size, manifest.segment_seconds)) {
    fprintf(stderr, "Initialization is durable despite telemetry failure\n");
  }
  bool output_ok = printf(
      "Initialized %s: %zu tasks, %zu recovery units, %u-second segments\n",
      options->state_directory, manifest.task_count,
      manifest.unit_count, manifest.segment_seconds) >= 0;
  if (!flush_stdout_checked("initialization summary")) {
    output_ok = false;
  }
  if (!output_ok) {
    fprintf(stderr,
            "Initialization is durable despite standard-output failure\n");
    ok = false;
  }
  free_manifest(&manifest);
  return ok;
}

static bool show_status(const char *directory) {
  Manifest manifest;
  if (!load_manifest(directory, &manifest, false)) {
    return false;
  }
  const char *state = manifest.segment_open ? "interrupted" : "ready";
  if (manifest.completed_units == manifest.unit_count) {
    char result_path[PATH_MAX];
    struct stat result_stat;
    if (!make_path(result_path, directory, "results.tsv")) {
      free_manifest(&manifest);
      return false;
    }
    if (lstat(result_path, &result_stat) == 0) {
      uint64_t results[MAX_STEPS + 1];
      if (!S_ISREG(result_stat.st_mode)
          || !parse_results_file(result_path, manifest.max_steps, results)) {
        fprintf(stderr, "Final result is not a canonical regular file\n");
        free_manifest(&manifest);
        return false;
      }
      state = "complete";
    } else if (errno == ENOENT) {
      state = "finalization_pending";
    } else {
      fprintf(stderr, "Cannot inspect final result '%s': %s\n",
              result_path, strerror(errno));
      free_manifest(&manifest);
      return false;
    }
  }
  double total_elapsed = 0.0;
  for (size_t i = 0; i < manifest.completed_units; ++i) {
    total_elapsed += manifest.units[i].elapsed_seconds;
  }
  bool output_ok = true;
  if (printf("program\t%s\n", PROGRAM_ID) < 0
      || printf("source_sha256\t%s\n", manifest.source_sha) < 0
      || printf("makefile_sha256\t%s\n", manifest.makefile_sha) < 0
      || printf("state\t%s\n", state) < 0
      || printf("max_n\t%d\n", manifest.max_steps) < 0
      || printf("split_depth\t%d\n", manifest.split_depth) < 0
      || printf("threads\t%d\n", manifest.threads) < 0
      || printf("tasks\t%zu\n", manifest.task_count) < 0
      || printf("units\t%zu\n", manifest.unit_count) < 0
      || printf("completed_units\t%zu\n", manifest.completed_units) < 0
      || printf("next_unit\t%zu\n", manifest.completed_units) < 0
      || printf("segment\t%u\n", manifest.segment_id) < 0
      || printf("segment_open\t%s\n",
                manifest.segment_open ? "yes" : "no") < 0
      || printf("segment_elapsed_s\t%.3f\n", manifest.segment_elapsed) < 0
      || printf("compute_elapsed_s\t%.3f\n", total_elapsed) < 0) {
    output_ok = false;
  }
  if (!flush_stdout_checked("campaign status")) {
    output_ok = false;
  }
  free_manifest(&manifest);
  return output_ok;
}

static bool verify_campaign(const Options *options) {
  Manifest manifest;
  if (!load_manifest(options->state_directory, &manifest, false)) {
    return false;
  }
  BuildContext build;
  if (!prepare_build_context(manifest.max_steps, manifest.split_depth, &build)) {
    free_manifest(&manifest);
    return false;
  }
  bool ok = build.tasks.size == manifest.task_count;
  if (!ok) {
    fprintf(stderr, "Deterministic task count differs from manifest\n");
  }
  uint64_t aggregate[MAX_STEPS + 1];
  if (ok) {
    ok = aggregate_counts(&build, &manifest, aggregate);
  }
  if (ok && manifest.completed_units != manifest.unit_count) {
    fprintf(stderr, "Campaign is valid but incomplete: %zu/%zu units\n",
            manifest.completed_units, manifest.unit_count);
    ok = false;
  }
  char result_path[PATH_MAX];
  uint64_t results[MAX_STEPS + 1];
  if (ok && (!make_path(result_path, options->state_directory, "results.tsv")
             || !parse_results_file(result_path, manifest.max_steps, results)
             || !results_equal(aggregate, results, manifest.max_steps))) {
    fprintf(stderr, "Final result does not match the manifest aggregate\n");
    ok = false;
  }
  if (ok && options->verification_path != NULL) {
    ok = verify_p3_file(options->verification_path, aggregate,
                      manifest.max_steps);
  }
  if (ok) {
    fprintf(stderr, "CAMPAIGN VERIFY PASS: %zu/%zu units, n=1..%d\n",
            manifest.completed_units, manifest.unit_count,
            manifest.max_steps);
  }
  free_build_context(&build);
  free_manifest(&manifest);
  return ok;
}

static int advance_campaign(const Options *options, const bool is_resume) {
  const int lock_fd = acquire_lock(options->state_directory);
  if (lock_fd < 0) {
    return EXIT_FAILURE;
  }
  Manifest manifest;
  bool ok = load_manifest(options->state_directory, &manifest, true);
  if (!ok) {
    release_lock(lock_fd);
    return EXIT_FAILURE;
  }
  if (manifest.completed_units == manifest.unit_count) {
    char result_path[PATH_MAX];
    struct stat result_status;
    if (!make_path(result_path, options->state_directory, "results.tsv")) {
      free_manifest(&manifest);
      release_lock(lock_fd);
      return EXIT_FAILURE;
    }
    if (lstat(result_path, &result_status) == 0) {
      fprintf(stderr, "Campaign is already complete; refusing to clobber it\n");
      free_manifest(&manifest);
      release_lock(lock_fd);
      return EXIT_FAILURE;
    }
    if (errno != ENOENT) {
      fprintf(stderr, "Cannot inspect final result: %s\n", strerror(errno));
      free_manifest(&manifest);
      release_lock(lock_fd);
      return EXIT_FAILURE;
    }
    BuildContext recovery_build = {0};
    uint64_t recovery_counts[MAX_STEPS + 1];
    ok = prepare_build_context(manifest.max_steps, manifest.split_depth,
                               &recovery_build)
      && recovery_build.tasks.size == manifest.task_count
      && aggregate_counts(&recovery_build, &manifest, recovery_counts)
      && write_results_atomic(options->state_directory, recovery_counts,
                              manifest.max_steps)
      && emit_event("FINALIZE",
                    "recovered completed manifest at generation=%" PRIu64,
                    manifest.generation);
    if (ok) {
      fprintf(stderr, "Recovered final result from completed manifest\n");
    }
    free_build_context(&recovery_build);
    free_manifest(&manifest);
    release_lock(lock_fd);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  BuildContext build;
  ok = prepare_build_context(manifest.max_steps, manifest.split_depth, &build);
  if (!ok || build.tasks.size != manifest.task_count) {
    if (ok) {
      fprintf(stderr, "Deterministic task identity mismatch\n");
      free_build_context(&build);
    }
    free_manifest(&manifest);
    release_lock(lock_fd);
    return EXIT_FAILURE;
  }

  if (stop_requested) {
    ok = emit_event("STOP", "before segment start at unit=%zu/%zu",
                    manifest.completed_units, manifest.unit_count);
    free_build_context(&build);
    free_manifest(&manifest);
    if (!release_lock(lock_fd)) {
      ok = false;
    }
    return ok ? HANDLED_STOP_EXIT : EXIT_FAILURE;
  }

  if (!manifest.segment_open) {
    ++manifest.segment_id;
    manifest.segment_open = true;
    manifest.segment_elapsed = 0.0;
    ++manifest.generation;
    ok = write_manifest_atomic(options->state_directory, &manifest);
  }
  if (ok) {
    ok = emit_event(is_resume ? "RESUME" : "START",
                    "segment=%u from_unit=%zu total_units=%zu budget_s=%u threads=%d",
                    manifest.segment_id, manifest.completed_units,
                    manifest.unit_count, manifest.segment_seconds,
                    manifest.threads);
  }
  double last_heartbeat = wall_time();

  while (ok && manifest.completed_units < manifest.unit_count
         && manifest.segment_elapsed < manifest.segment_seconds) {
    const size_t unit_id = manifest.completed_units;
    UnitRecord *unit = &manifest.units[unit_id];
    unit->task_begin = unit_id * manifest.unit_size;
    unit->task_end = unit->task_begin + manifest.unit_size;
    if (unit->task_end > manifest.task_count) {
      unit->task_end = manifest.task_count;
    }
    unit->segment_id = manifest.segment_id;
    const double unit_start = wall_time();
    ok = execute_task_range(&build, unit->task_begin, unit->task_end,
                            manifest.threads, unit->counts);
    unit->elapsed_seconds = wall_time() - unit_start;
    if (!ok) {
      break;
    }
    manifest.segment_elapsed += unit->elapsed_seconds;
    ++manifest.completed_units;
    ++manifest.generation;
    if (manifest.completed_units == manifest.unit_count) {
      manifest.segment_open = false;
    }
    ok = write_manifest_atomic(options->state_directory, &manifest);
    const double now = wall_time();
    if (ok && (now - last_heartbeat >= HEARTBEAT_SECONDS
               || manifest.completed_units == manifest.unit_count)) {
      ok = emit_event("HEARTBEAT",
                      "segment=%u units=%zu/%zu segment_s=%.3f",
                      manifest.segment_id, manifest.completed_units,
                      manifest.unit_count, manifest.segment_elapsed);
      last_heartbeat = now;
    }
    if (stop_requested) {
      break;
    }
  }

  if (ok && manifest.segment_open) {
    manifest.segment_open = false;
    ++manifest.generation;
    ok = write_manifest_atomic(options->state_directory, &manifest);
  }
  if (ok && manifest.completed_units == manifest.unit_count) {
    uint64_t counts[MAX_STEPS + 1];
    ok = aggregate_counts(&build, &manifest, counts)
      && write_results_atomic(options->state_directory, counts,
                              manifest.max_steps)
      && emit_event("COMPLETE",
                    "segments=%u units=%zu n=%d last_segment_s=%.3f",
                    manifest.segment_id, manifest.completed_units,
                    manifest.max_steps, manifest.segment_elapsed);
  } else if (ok) {
    ok = emit_event(stop_requested ? "STOP" : "SEGMENT_END",
                    "segment=%u units=%zu/%zu segment_s=%.3f",
                    manifest.segment_id, manifest.completed_units,
                    manifest.unit_count, manifest.segment_elapsed);
  }

  const bool campaign_complete =
      ok && manifest.completed_units == manifest.unit_count;
  free_build_context(&build);
  free_manifest(&manifest);
  if (!release_lock(lock_fd)) {
    ok = false;
  }
  if (!ok) {
    return EXIT_FAILURE;
  }
  if (stop_requested && !campaign_complete) {
    return HANDLED_STOP_EXIT;
  }
  return EXIT_SUCCESS;
}

static bool parse_command(const char *text, Command *command) {
  if (strcmp(text, "compute") == 0) {
    *command = COMMAND_COMPUTE;
  } else if (strcmp(text, "init") == 0) {
    *command = COMMAND_INIT;
  } else if (strcmp(text, "next") == 0) {
    *command = COMMAND_NEXT;
  } else if (strcmp(text, "resume") == 0) {
    *command = COMMAND_RESUME;
  } else if (strcmp(text, "status") == 0) {
    *command = COMMAND_STATUS;
  } else if (strcmp(text, "verify") == 0) {
    *command = COMMAND_VERIFY;
  } else {
    return false;
  }
  return true;
}

static bool parse_options(const int argc, char **argv, Options *options) {
  memset(options, 0, sizeof(*options));
  options->threads = 1;
  options->split_depth = DEFAULT_SPLIT_DEPTH;
  options->unit_size = DEFAULT_UNIT_SIZE;
  options->segment_seconds = DEFAULT_SEGMENT_SECONDS;
  if (argc < 2 || !parse_command(argv[1], &options->command)) {
    return false;
  }
  bool seen_max = false;
  bool seen_threads = false;
  bool seen_split = false;
  bool seen_unit = false;
  bool seen_segment = false;
  bool seen_state = false;
  bool seen_p3_file = false;
  for (int i = 2; i < argc; ++i) {
    if (strcmp(argv[i], "--max-n") == 0 && i + 1 < argc) {
      if (seen_max || (options->command != COMMAND_COMPUTE
          && options->command != COMMAND_INIT)) {
        return false;
      }
      seen_max = true;
      if (!parse_int(argv[++i], &options->max_steps)) {
        return false;
      }
    } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      if (seen_threads || (options->command != COMMAND_COMPUTE
          && options->command != COMMAND_INIT)) {
        return false;
      }
      seen_threads = true;
      if (!parse_int(argv[++i], &options->threads)) {
        return false;
      }
    } else if (strcmp(argv[i], "--split-depth") == 0 && i + 1 < argc) {
      if (seen_split || (options->command != COMMAND_COMPUTE
          && options->command != COMMAND_INIT)) {
        return false;
      }
      seen_split = true;
      if (!parse_int(argv[++i], &options->split_depth)) {
        return false;
      }
    } else if (strcmp(argv[i], "--unit-size") == 0 && i + 1 < argc) {
      if (seen_unit || options->command != COMMAND_INIT) {
        return false;
      }
      seen_unit = true;
      if (!parse_size(argv[++i], &options->unit_size)) {
        return false;
      }
    } else if (strcmp(argv[i], "--segment-seconds") == 0
               && i + 1 < argc) {
      if (seen_segment || options->command != COMMAND_INIT) {
        return false;
      }
      seen_segment = true;
      if (!parse_unsigned(argv[++i], &options->segment_seconds)) {
        return false;
      }
    } else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
      if (seen_state || options->command == COMMAND_COMPUTE) {
        return false;
      }
      seen_state = true;
      options->state_directory = argv[++i];
    } else if (strcmp(argv[i], "--p3-file") == 0 && i + 1 < argc) {
      if (seen_p3_file || (options->command != COMMAND_COMPUTE
          && options->command != COMMAND_VERIFY)) {
        return false;
      }
      seen_p3_file = true;
      options->verification_path = argv[++i];
    } else {
      return false;
    }
  }

  if (options->command == COMMAND_COMPUTE
      || options->command == COMMAND_INIT) {
    if (options->max_steps < 1 || options->max_steps > MAX_STEPS) {
      return false;
    }
    if (options->split_depth > options->max_steps) {
      if (seen_split) {
        return false;
      }
      options->split_depth = options->max_steps;
    }
    if (options->split_depth < 1
        || options->split_depth > MAX_SPLIT_DEPTH
        || options->threads < 1 || options->threads > MAX_THREADS) {
      return false;
    }
  }
  if (options->command == COMMAND_INIT) {
    if (options->state_directory == NULL || options->unit_size == 0
        || options->segment_seconds == 0
        || options->segment_seconds > 5400) {
      return false;
    }
  } else if (options->command != COMMAND_COMPUTE
             && options->state_directory == NULL) {
    return false;
  }
  if ((options->command == COMMAND_NEXT
       || options->command == COMMAND_RESUME
       || options->command == COMMAND_STATUS)
      && options->verification_path != NULL) {
    return false;
  }
  return true;
}

static int run_compute_command(const Options *options, const char *program) {
  char max_text[32];
  char thread_text[32];
  char split_text[32];
  snprintf(max_text, sizeof(max_text), "%d", options->max_steps);
  snprintf(thread_text, sizeof(thread_text), "%d", options->threads);
  snprintf(split_text, sizeof(split_text), "%d", options->split_depth);
  char *arguments[10];
  int count = 0;
  arguments[count++] = (char *) program;
  arguments[count++] = "--max-n";
  arguments[count++] = max_text;
  arguments[count++] = "--threads";
  arguments[count++] = thread_text;
  arguments[count++] = "--split-depth";
  arguments[count++] = split_text;
  if (options->verification_path != NULL) {
    arguments[count++] = "--p3-file";
    arguments[count++] = (char *) options->verification_path;
  }
  arguments[count] = NULL;
  return run_legacy_compute(count, arguments);
}

int main(int argc, char **argv) {
  const bool writer_command_requested = argc >= 2
    && (strcmp(argv[1], "next") == 0 || strcmp(argv[1], "resume") == 0);
  if (writer_command_requested && !install_writer_stop_handlers()) {
    return EXIT_FAILURE;
  }
  if (argc == 2
      && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }
  Options options;
  if (!parse_options(argc, argv, &options)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }
#ifndef _OPENMP
  if ((options.command == COMMAND_COMPUTE
       || options.command == COMMAND_INIT) && options.threads != 1) {
    fprintf(stderr, "This binary was built without OpenMP; use --threads 1\n");
    return EXIT_FAILURE;
  }
#endif
  switch (options.command) {
    case COMMAND_COMPUTE:
      return run_compute_command(&options, argv[0]);
    case COMMAND_INIT:
      return initialize_campaign(&options) ? EXIT_SUCCESS : EXIT_FAILURE;
    case COMMAND_NEXT:
      return advance_campaign(&options, false);
    case COMMAND_RESUME:
      return advance_campaign(&options, true);
    case COMMAND_STATUS:
      return show_status(options.state_directory) ? EXIT_SUCCESS : EXIT_FAILURE;
    case COMMAND_VERIFY:
      return verify_campaign(&options) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  return EXIT_FAILURE;
}
