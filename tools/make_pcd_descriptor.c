#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
};

struct descriptor_config {
    const char *name_chunk;
    const char *name;
    const char *base_name;
    const char *pattern;
    unsigned char group_id[4];
    unsigned char type_id[4];
    uint16_t flags;
    uint16_t priority;
    const char *fver;
    const char *dtcd_path;
    const char *output_path;
};

static int buffer_reserve(struct buffer *buf, size_t extra)
{
    unsigned char *new_data;
    size_t needed;
    size_t new_capacity;

    needed = buf->size + extra;
    if (needed <= buf->capacity) {
        return 0;
    }

    new_capacity = buf->capacity == 0 ? 128 : buf->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    new_data = realloc(buf->data, new_capacity);
    if (new_data == NULL) {
        return -1;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
    return 0;
}

static int buffer_append(struct buffer *buf, const void *data, size_t len)
{
    if (buffer_reserve(buf, len) != 0) {
        return -1;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return 0;
}

static int buffer_append_u16be(struct buffer *buf, uint16_t value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char)((value >> 8) & 0xff);
    bytes[1] = (unsigned char)(value & 0xff);
    return buffer_append(buf, bytes, sizeof(bytes));
}

static int buffer_append_u32be(struct buffer *buf, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)((value >> 24) & 0xff);
    bytes[1] = (unsigned char)((value >> 16) & 0xff);
    bytes[2] = (unsigned char)((value >> 8) & 0xff);
    bytes[3] = (unsigned char)(value & 0xff);
    return buffer_append(buf, bytes, sizeof(bytes));
}

static int buffer_append_chunk(struct buffer *form, const char id[4], const void *data, size_t len)
{
    static const unsigned char pad = 0;

    if (buffer_append(form, id, 4) != 0 ||
        buffer_append_u32be(form, (uint32_t)len) != 0 ||
        buffer_append(form, data, len) != 0) {
        return -1;
    }

    if ((len & 1U) != 0 && buffer_append(form, &pad, 1) != 0) {
        return -1;
    }

    return 0;
}

static int read_file(const char *path, unsigned char **data_out, size_t *size_out)
{
    FILE *fp;
    long size;
    unsigned char *data;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    data = malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    if (size > 0 && fread(data, (size_t)size, 1, fp) != 1) {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *data_out = data;
    *size_out = (size_t)size;
    return 0;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *fp;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }

    if (len > 0 && fwrite(data, len, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        return -1;
    }

    return 0;
}

static int build_dthd(const struct descriptor_config *cfg, struct buffer *dthd)
{
    uint32_t name_offset = 32;
    uint32_t base_offset = name_offset + (uint32_t)(strlen(cfg->name) + 1);
    uint32_t pattern_offset = base_offset + (uint32_t)(strlen(cfg->base_name) + 1);
    uint32_t mask_offset = 0;

    if (buffer_append_u32be(dthd, name_offset) != 0 ||
        buffer_append_u32be(dthd, base_offset) != 0 ||
        buffer_append_u32be(dthd, pattern_offset) != 0 ||
        buffer_append_u32be(dthd, mask_offset) != 0 ||
        buffer_append(dthd, cfg->group_id, 4) != 0 ||
        buffer_append(dthd, cfg->type_id, 4) != 0 ||
        buffer_append_u16be(dthd, 0) != 0 ||
        buffer_append_u16be(dthd, 0) != 0 ||
        buffer_append_u16be(dthd, cfg->flags) != 0 ||
        buffer_append_u16be(dthd, cfg->priority) != 0 ||
        buffer_append(dthd, cfg->name, strlen(cfg->name) + 1) != 0 ||
        buffer_append(dthd, cfg->base_name, strlen(cfg->base_name) + 1) != 0 ||
        buffer_append(dthd, cfg->pattern, strlen(cfg->pattern) + 1) != 0) {
        return -1;
    }

    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [-o output] [--fver text] [--dtcd file]\n",
        argv0);
}

static int parse_args(int argc, char **argv, struct descriptor_config *cfg)
{
    int i;

    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            cfg->output_path = argv[++i];
        } else if (strcmp(argv[i], "--fver") == 0 && i + 1 < argc) {
            cfg->fver = argv[++i];
        } else if (strcmp(argv[i], "--dtcd") == 0 && i + 1 < argc) {
            cfg->dtcd_path = argv[++i];
        } else {
            usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct descriptor_config cfg = {
        .name_chunk = "PhotoCD",
        .name = "PhotoCD",
        .base_name = "photocd",
        .pattern = "#?.pcd",
        .group_id = { 'p', 'i', 'c', 't' },
        .type_id = { 'p', 'c', 'd', '\0' },
        .flags = 0,
        .priority = 0,
        .fver = NULL,
        .dtcd_path = NULL,
        .output_path = "build/amiga/Devs/DataTypes/PhotoCD",
    };
    struct buffer dthd = {0};
    struct buffer form = {0};
    struct buffer file = {0};
    unsigned char *dtcd_data = NULL;
    size_t dtcd_size = 0;
    int rc = 1;

    if (parse_args(argc, argv, &cfg) != 0) {
        goto cleanup;
    }

    if (build_dthd(&cfg, &dthd) != 0) {
        perror("build_dthd");
        goto cleanup;
    }

    if (buffer_append_chunk(&form, "NAME", cfg.name_chunk, strlen(cfg.name_chunk) + 1) != 0 ||
        buffer_append_chunk(&form, "DTHD", dthd.data, dthd.size) != 0) {
        perror("buffer_append_chunk");
        goto cleanup;
    }

    if (cfg.fver != NULL &&
        buffer_append_chunk(&form, "FVER", cfg.fver, strlen(cfg.fver) + 1) != 0) {
        perror("buffer_append_chunk");
        goto cleanup;
    }

    if (cfg.dtcd_path != NULL) {
        if (read_file(cfg.dtcd_path, &dtcd_data, &dtcd_size) != 0) {
            perror(cfg.dtcd_path);
            goto cleanup;
        }

        if (buffer_append_chunk(&form, "DTCD", dtcd_data, dtcd_size) != 0) {
            perror("buffer_append_chunk");
            goto cleanup;
        }
    }

    if (buffer_append(&file, "FORM", 4) != 0 ||
        buffer_append_u32be(&file, (uint32_t)(4 + form.size)) != 0 ||
        buffer_append(&file, "DTYP", 4) != 0 ||
        buffer_append(&file, form.data, form.size) != 0) {
        perror("buffer_append");
        goto cleanup;
    }

    if (write_file(cfg.output_path, file.data, file.size) != 0) {
        perror(cfg.output_path);
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(dtcd_data);
    free(dthd.data);
    free(form.data);
    free(file.data);
    return rc;
}
