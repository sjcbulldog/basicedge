/******************************************************************************
 * File: basic_catalog.h
 * Description: Bounded parser for the remote BASIC program catalog.
 ******************************************************************************/
#ifndef BASIC_CATALOG_H
#define BASIC_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BASIC_CATALOG_MAX_PROGRAMS      (32U)
#define BASIC_CATALOG_FILENAME_SIZE     (64U)
#define BASIC_CATALOG_TYPE_SIZE         (32U)
#define BASIC_CATALOG_DESCRIPTION_SIZE  (192U)

typedef struct
{
    char filename[BASIC_CATALOG_FILENAME_SIZE];
    char type[BASIC_CATALOG_TYPE_SIZE];
    char description[BASIC_CATALOG_DESCRIPTION_SIZE];
    uint32_t size;
} basic_catalog_program_t;

typedef struct
{
    basic_catalog_program_t programs[BASIC_CATALOG_MAX_PROGRAMS];
    size_t count;
} basic_catalog_t;

/**
 * @brief Parse a catalog containing a top-level "programs" array.
 * @param[in] json Catalog bytes; a terminating NUL is not required.
 * @param[in] length Number of catalog bytes.
 * @param[out] catalog Parsed catalog; unchanged when parsing fails.
 * @return true when the catalog is valid.
 */
bool basic_catalog_parse(const char *json, size_t length, basic_catalog_t *catalog);

/**
 * @brief Find a program by filename, with an optional omitted ".bas" suffix.
 * @return Matching program, or NULL when none exists.
 */
const basic_catalog_program_t *basic_catalog_find(const basic_catalog_t *catalog,
                                                  const char *name);

#endif /* BASIC_CATALOG_H */
