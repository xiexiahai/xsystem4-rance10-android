/* Activity metadata is separate from both live Parts and back-scene capture. */
#ifndef XSYSTEM4_PARTS_ACTIVITY_H
#define XSYSTEM4_PARTS_ACTIVITY_H

#include <stdbool.h>
struct string;
struct page;
struct iarray_writer;
struct iarray_reader;
struct parts_activity_snapshot;

/* Native XPE persistence; XBS snapshots deliberately exclude live activities. */
void parts_activity_save(struct iarray_writer *w, bool save_hidden);
bool parts_activity_read(struct iarray_reader *r, struct parts_activity_snapshot **out);
bool parts_activity_install(struct parts_activity_snapshot *snapshot);
void parts_activity_snapshot_free(struct parts_activity_snapshot *snapshot);
bool parts_activity_has_part(int number);

void PE_ActivityReset(void);
void PE_ActivityPartReleased(int number);
bool PE_IsExistActivity(struct string *name);
bool PE_CreateActivity(struct string *name);
bool PE_ReleaseActivity(struct string *name, struct page **delegates);
bool PE_ReadActivityFile(struct string *name, struct string *filename);
bool PE_ReadActivityFileEx(struct string *name, struct string *filename, bool edit);
bool PE_AddActivityParts(struct string *name, struct string *parts_name, int number);
bool PE_RemoveActivityParts(struct string *name, struct string *parts_name);
bool PE_RemoveAllActivityParts(struct string *name);
int PE_NumofActivityParts(struct string *name);
bool PE_GetActivityParts(int index, struct string *name, struct string **parts_name, int *number);
bool PE_IsExistActivityPartsByName(struct string *name, struct string *parts_name);
bool PE_IsExistActivityPartsByNumber(struct string *name, int number);
int PE_GetActivityPartsNumber(struct string *name, struct string *parts_name);
void PE_GetActivityPartsName(struct string **out, struct string *name, int number);
void PE_AddActivityCloseParts(struct string *name, struct string *parts_name);
void PE_RemoveActivityCloseParts(struct string *name, struct string *parts_name);
void PE_RemoveAllActivityCloseParts(struct string *name);
bool PE_IsExistActivityCloseParts(struct string *name, struct string *parts_name);
void PE_SetActivityIntentData(struct string *name, struct string *parts_name, struct string *destination, int type);
void PE_AddActivityIntentDataDestination(struct string *name, struct string *parts_name, struct string *destination);
bool PE_IsExistActivityIntentData(struct string *name, struct string *parts_name);
int PE_NumofActivityIntentDataDestination(struct string *name, struct string *parts_name);
void PE_GetActivityIntentDataDestination(struct string **out, int index, struct string *name, struct string *parts_name);
int PE_GetActivityIntentDataType(struct string *name, struct string *parts_name);
void PE_SetActivityEndKey(struct string *name, int key);
void PE_EraseActivityEndKey(struct string *name, int key);
bool PE_IsExistActivityEndKey(struct string *name, int key);
int PE_NumofActivityEndKey(struct string *name);
int PE_GetActivityEndKey(struct string *name, int index);

#endif
