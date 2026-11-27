#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const allowed_files[] = {
    "Rance10.ain", "Rance10EX.ex", "Rance10MODCG.afa", "Rance10MODCG2.afa",
    "Rance10ModFlat.afa", "Rance10Pact.afa", "Rance10Sound.afa"
};

static const char *asset_name(const char *path)
{
    if (!path)
        return NULL;
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    for (size_t i = 0; i < sizeof allowed_files / sizeof *allowed_files; ++i)
        if (!strcasecmp(name, allowed_files[i]))
            return allowed_files[i];
    return NULL;
}

static bool write_entry(struct archive *reader, const char *path, size_t limit)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    FILE *output = fdopen(fd, "wb");
    if (!output) {
        close(fd);
        return false;
    }
    unsigned char buffer[65536];
    bool success = false;
    size_t total = 0;
    for (;;) {
        la_ssize_t count = archive_read_data(reader, buffer, sizeof buffer);
        if (count == 0) {
            success = total == limit;
            break;
        }
        if (count < 0 || (size_t)count > limit - total ||
            fwrite(buffer, 1, (size_t)count, output) != (size_t)count)
            break;
        total += (size_t)count;
    }
    return fclose(output) == 0 && success;
}

static bool extract(const char *input, const char *directory)
{
    struct archive *reader = archive_read_new();
    if (!reader)
        return false;
    bool success = false;
    if (archive_read_support_format_rar5(reader) != ARCHIVE_OK ||
        archive_read_open_filename(reader, input, 65536) != ARCHIVE_OK)
        goto done;

    size_t capacity = strlen(directory) + 40;
    char *destination = malloc(capacity);
    if (!destination)
        goto done;
    struct archive_entry *entry;
    int status;
    while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const char *name = asset_name(archive_entry_pathname(entry));
        if (!name || archive_entry_filetype(entry) != AE_IFREG || archive_entry_hardlink(entry))
            continue;
        snprintf(destination, capacity, "%s/%s", directory, name);
        struct stat info;
        if (lstat(destination, &info) == 0) {
            if (!S_ISREG(info.st_mode))
                break;
            continue;
        }
        la_int64_t size = archive_entry_size(entry);
        if (errno != ENOENT || size <= 0 || size > 1024LL * 1024 * 1024 ||
            !write_entry(reader, destination, (size_t)size))
            break;
    }
    success = status == ARCHIVE_EOF;
    free(destination);
done:
    archive_read_free(reader);
    return success;
}

JNIEXPORT jboolean JNICALL Java_io_github_kichikuou_xsystem4_SunsetArchive_extract(
    JNIEnv *env, jobject self, jstring archive_path, jstring destination_dir)
{
    (void)self;
    const char *archive = (*env)->GetStringUTFChars(env, archive_path, NULL);
    if (!archive)
        return JNI_FALSE;
    const char *directory = (*env)->GetStringUTFChars(env, destination_dir, NULL);
    bool success = directory && extract(archive, directory);
    if (directory)
        (*env)->ReleaseStringUTFChars(env, destination_dir, directory);
    (*env)->ReleaseStringUTFChars(env, archive_path, archive);
    return success ? JNI_TRUE : JNI_FALSE;
}
