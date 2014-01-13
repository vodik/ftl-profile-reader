#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>

enum difficulty {
    DIFFICULTY_EASY,
    DIFFICULTY_NORMAL
};

typedef struct ftl_achievement_t {
    char *achievement;
    int32_t difficulty;
} ftl_achievement;

typedef struct ftl_score_t {
    char *ship_name;
    char *ship_type;
    int32_t score;
    int32_t sector;
    int32_t victory;
    int32_t difficulty;
} ftl_score;

typedef struct ftl_crew_t {
    char *name;
    char *race;
    int32_t score;
    int32_t gender;
} ftl_crew;

typedef struct ftl_profile_t {
    int32_t ship_kestrel;
    int32_t ship_stealth;
    int32_t ship_mantis;
    int32_t ship_engi;
    int32_t ship_federation;
    int32_t ship_slug;
    int32_t ship_rock;
    int32_t ship_zoltan;
    int32_t ship_crystal;

    int32_t best_ships;
    int32_t best_beacons;
    int32_t best_scrap;
    int32_t best_crew;
    int32_t total_ships;
    int32_t total_beacons;
    int32_t total_scrap;
    int32_t total_crew;
    int32_t games;
    int32_t victories;

    int32_t achievement_count;
    int32_t high_score_count;
    int32_t ship_score_count;

    ftl_achievement *achievements;
    ftl_score *high_scores;
    ftl_score *ship_scores;
    ftl_crew crew[5];
} ftl_profile;

/* {{{ 1 */
static ssize_t read_int32s(int fd, int num, ...)
{
    int i;
    va_list ap;
    struct iovec iov[num];

    va_start(ap, num);
    for (i = 0; i < num; ++i) {
        iov[i].iov_base = va_arg(ap, int32_t *);
        iov[i].iov_len  = sizeof(int32_t);
    }
    va_end(ap);

    return readv(fd, iov, num);
}

static ssize_t write_int32s(int fd, int num, ...)
{
    int i;
    va_list ap;
    struct iovec iov[num];

    va_start(ap, num);
    for (i = 0; i < num; ++i) {
        iov[i].iov_base = va_arg(ap, int32_t *);
        iov[i].iov_len  = sizeof(int32_t);
    }
    va_end(ap);

    return writev(fd, iov, num);
}

static inline ssize_t read_int32(int fd, int32_t *val)
{
    return read(fd, val, sizeof(int32_t));
}

static inline ssize_t write_int32(int fd, int32_t val)
{
    return write(fd, &val, sizeof(int32_t));
}

static ssize_t read_string(int fd, char **val)
{
    int32_t length;
    ssize_t ret, nbytes_r = read_int32(fd, &length);
    if (nbytes_r < 0)
        return nbytes_r;

    char *buf = malloc(length + 1);
    ret = read(fd, buf, length);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    buf[length] = 0;
    *val = buf;
    return nbytes_r + ret;
}

static ssize_t write_string(int fd, char *val)
{
    int32_t length = strlen(val);
    ssize_t ret, nbytes_r = write_int32(fd, length);
    if (nbytes_r < 0)
        return nbytes_r;

    ret = write(fd, val, length);
    if (ret < 0)
        return ret;

    return nbytes_r + ret;
}

static inline void *read_alloc_array(int fd, int32_t *len, size_t elem_size)
{
    read_int32(fd, len);
    return malloc(elem_size * *len);
}
/* }}} */

static inline void read_achievement(int fd, ftl_achievement *a)
{
    read_string(fd, &a->achievement);
    read_int32(fd, &a->difficulty);
}

static inline void write_achievements(int fd, ftl_achievement *a)
{
    write_string(fd, a->achievement);
    write_int32(fd, a->difficulty);
}

static inline void read_score(int fd, ftl_score *h)
{
    read_string(fd, &h->ship_name);
    read_string(fd, &h->ship_type);
    read_int32s(fd, 4, &h->score,
                &h->sector,
                &h->victory,
                &h->difficulty);
}

static inline void write_score(int fd, ftl_score *h)
{
    write_string(fd, h->ship_name);
    write_string(fd, h->ship_type);
    write_int32s(fd, 4, &h->score,
                 &h->sector,
                 &h->victory,
                 &h->difficulty);
}

static inline void read_crew(int fd, ftl_crew *c)
{
    read_int32(fd, &c->score);
    read_string(fd, &c->name);
    read_string(fd, &c->race);
    read_int32(fd, &c->gender);
}

static inline void write_crew(int fd, ftl_crew *c)
{
    write_int32(fd, c->score);
    write_string(fd, c->name);
    write_string(fd, c->race);
    write_int32(fd, c->gender);
}

static void dump_profile(ftl_profile *p) /* {{{ */
{
    static const char *types[] = {
        "repair",
        "combat",
        "pilot",
        "jumps",
        "skills"
    };
    int i;

    printf("ships:\n");
    printf(" - Kestrel: %s\n",    p->ship_kestrel    ? "unlocked" : "locked");
    printf(" - Stealth: %s\n",    p->ship_stealth    ? "unlocked" : "locked");
    printf(" - Mantis: %s\n",     p->ship_mantis     ? "unlocked" : "locked");
    printf(" - Engi: %s\n",       p->ship_engi       ? "unlocked" : "locked");
    printf(" - Federation: %s\n", p->ship_federation ? "unlocked" : "locked");
    printf(" - Slug: %s\n",       p->ship_slug       ? "unlocked" : "locked");
    printf(" - Rock: %s\n",       p->ship_rock       ? "unlocked" : "locked");
    printf(" - Zoltan: %s\n",     p->ship_zoltan     ? "unlocked" : "locked");
    printf(" - Crystal: %s\n",    p->ship_crystal    ? "unlocked" : "locked");

    printf("achievements:\n");
    for (i = 0; i < p->achievement_count; ++i) {
        ftl_achievement *a = &p->achievements[i];

        printf(" - %s [%s]\n", a->achievement,
               a->difficulty ? "normal" : "easy");
    }

    printf("high score:\n");
    for (i = 0; i < p->high_score_count; ++i) {
        ftl_score *h = &p->high_scores[i];

        printf(" - ship: %s [%s]\n", h->ship_name, h->ship_type);
        printf(" - score: %d on %s%s\n", h->score,
               h->difficulty ? "easy" : "normal",
               h->victory ? " [victory]" : "");
    }

    printf("ship score:\n");
    for (i = 0; i < p->ship_score_count; ++i) {
        ftl_score *h = &p->ship_scores[i];

        printf(" - ship: %s [%s]\n", h->ship_name, h->ship_type);
        printf(" - score: %d on %s%s\n", h->score,
               h->difficulty ? "easy" : "normal",
               h->victory ? " [victory]" : "");
    }

    printf("stats:\n");
    printf(" - best ships defeated: %d\n", p->best_ships);
    printf(" - best number of beacons explored: %d\n", p->best_beacons);
    printf(" - best scrap collected: %d\n", p->best_scrap);
    printf(" - best number of crew hired: %d\n", p->best_crew);
    printf(" - total ships defeated: %d\n", p->total_ships);
    printf(" - total number of beacons explored: %d\n", p->total_beacons);
    printf(" - total scrap collected: %d\n", p->total_scrap);
    printf(" - total number of crew hired: %d\n", p->total_crew);

    printf("crew:\n");
    for (i = 0; i < 5; ++i) {
        ftl_crew *c = &p->crew[i];

        printf(" - best %s: %s %s named %s with score of %d\n",
               types[i],
               c->gender ? "male" : "female",
               c->race,
               c->name,
               c->score);
    }

    printf("total number of games played: %d\n", p->games);
    printf("total number of victories: %d\n", p->victories);
}
/* }}} */

static int read_ftl_profile(int fd, ftl_profile *p)
{
    int32_t version, i;

    read_int32(fd, &version);
    printf("version: %d\n", version);

    /* read achievements */
    p->achievements = read_alloc_array(fd, &p->achievement_count, sizeof(ftl_achievement));
    for (i = 0; i < p->achievement_count; ++i)
        read_achievement(fd, &p->achievements[i]);

    /* read ships */
    read_int32s(fd, 9, &p->ship_kestrel,
                &p->ship_stealth,
                &p->ship_mantis,
                &p->ship_engi,
                &p->ship_federation,
                &p->ship_slug,
                &p->ship_rock,
                &p->ship_zoltan,
                &p->ship_crystal);

    /* there are three unused ship slots */
    lseek(fd, sizeof(int32_t) * 3, SEEK_CUR);

    /* read highscores */
    p->high_scores = read_alloc_array(fd, &p->high_score_count, sizeof(ftl_score));
    for (i = 0; i < p->high_score_count; ++i)
        read_score(fd, &p->high_scores[i]);

    /* read ship scores */
    p->ship_scores = read_alloc_array(fd, &p->ship_score_count, sizeof(ftl_score));
    for (i = 0; i < p->ship_score_count; ++i)
        read_score(fd, &p->ship_scores[i]);

    /* read statistics */
    read_int32s(fd, 10, &p->best_ships,
                &p->total_ships,
                &p->best_beacons,
                &p->total_beacons,
                &p->best_scrap,
                &p->total_scrap,
                &p->best_crew,
                &p->total_crew,
                &p->games,
                &p->victories);

    /* read best crew */
    for (i = 0; i < 5; ++i)
        read_crew(fd, &p->crew[i]);

    return 0;
}

static int write_ftl_profile(int fd, ftl_profile *p)
{
    int32_t i, zero = 0;

    /* write version info */
    write_int32(fd, 4);

    /* write achievments */
    write_int32(fd, p->achievement_count);
    for (i = 0; i < p->achievement_count; ++i)
        write_achievements(fd, &p->achievements[i]);

    /* write ship unlocks */
    write_int32s(fd, 12, &p->ship_kestrel,
                 &p->ship_stealth,
                 &p->ship_mantis,
                 &p->ship_engi,
                 &p->ship_federation,
                 &p->ship_slug,
                 &p->ship_rock,
                 &p->ship_zoltan,
                 &p->ship_crystal,
                 &zero, &zero, &zero);

    /* write highscores */
    write_int32(fd, p->high_score_count);
    for (i = 0; i < p->high_score_count; ++i)
        write_score(fd, &p->high_scores[i]);

    /* write ship scores */
    write_int32(fd, p->ship_score_count);
    for (i = 0; i < p->ship_score_count; ++i)
        write_score(fd, &p->ship_scores[i]);

    /* write statistics */
    write_int32s(fd, 10, &p->best_ships,
                 &p->total_ships,
                 &p->best_beacons,
                 &p->total_beacons,
                 &p->best_scrap,
                 &p->total_scrap,
                 &p->best_crew,
                 &p->total_crew,
                 &p->games,
                 &p->victories);

    /* write statistics */
    for (i = 0; i < 5; ++i)
        write_crew(fd, &p->crew[i]);

    return 0;
}

int main(void)
{
    int fd;
    ftl_profile profile = { };

    fd = open("/home/simon/.local/share/FasterThanLight/prof.sav", O_RDONLY);
    if (fd < 0)
        err(1, "failed to open profile");

    read_ftl_profile(fd, &profile);
    close(fd);

    dump_profile(&profile);

    /* fd = open("/tmp/prof.sav", O_WRONLY | O_CREAT | O_TRUNC, 0644); */
    /* if (fd < 0) */
    /*     err(1, "failed to open profile"); */

    /* write_ftl_profile(fd, &profile); */
    /* close(fd); */
}
