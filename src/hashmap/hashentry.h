#ifndef HASHMAP_HASHENTRY_H_
#define HASHMAP_HASHENTRY_H_

/*
 * The motivation for this structure comes from the objective of keeping the
 * hashmap implementation clean. Didn't want to force a single TYPE for
 * everything that gets stored in hashmap.
 *
 * Right now we have a single hashEntryAPI_t per hashMap.
 *  - We can either keep multiple hashmaps for multiple type of objects
 *  - Get the hashEntryAPI_t from the object itself
 *  - Have a type registory
 */

typedef struct {
    u_int32_t  (*getKeyLength)(void* value);
    char*      (*getKey)(void* value);
    void       (*addReference)(void* value);
    void       (*onObjectDeleted)(void* context, void* value);
    u_int32_t  (*getExpiry)(void* value);
    u_int32_t  (*getTotalSize)(void* value);
    void*        context;
}hashEntryAPI_t;

#endif /* HASHMAP_HASHENTRY_H_ */
