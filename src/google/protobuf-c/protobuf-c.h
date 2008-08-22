#ifndef __PROTOBUF_C_RUNTIME_H_
#define __PROTOBUF_C_RUNTIME_H_

#include <inttypes.h>
#include <stddef.h>
#include <assert.h>


typedef enum
{
  PROTOBUF_C_LABEL_REQUIRED,
  PROTOBUF_C_LABEL_OPTIONAL,
  PROTOBUF_C_LABEL_REPEATED
} ProtobufCLabel;

typedef enum
{
  PROTOBUF_C_TYPE_INT32,
  PROTOBUF_C_TYPE_SINT32,
  PROTOBUF_C_TYPE_SFIXED32,
  PROTOBUF_C_TYPE_INT64,
  PROTOBUF_C_TYPE_SINT64,
  PROTOBUF_C_TYPE_SFIXED64,
  PROTOBUF_C_TYPE_UINT32,
  PROTOBUF_C_TYPE_FIXED32,
  PROTOBUF_C_TYPE_UINT64,
  PROTOBUF_C_TYPE_FIXED64,
  PROTOBUF_C_TYPE_FLOAT,
  PROTOBUF_C_TYPE_DOUBLE,
  PROTOBUF_C_TYPE_BOOL,
  PROTOBUF_C_TYPE_ENUM,
  PROTOBUF_C_TYPE_STRING,
  PROTOBUF_C_TYPE_BYTES,
  //PROTOBUF_C_TYPE_GROUP,          // NOT SUPPORTED
  PROTOBUF_C_TYPE_MESSAGE,
} ProtobufCType;

typedef int protobuf_c_boolean;
#define PROTOBUF_C_OFFSETOF(struct, member) offsetof(struct, member)

#define PROTOBUF_C_ASSERT(condition) assert(condition)
#define PROTOBUF_C_ASSERT_NOT_REACHED() assert(0)

typedef struct _ProtobufCBinaryData ProtobufCBinaryData;
struct _ProtobufCBinaryData
{
  size_t len;
  unsigned char *data;
};

typedef struct _ProtobufCIntRange ProtobufCIntRange; /* private */

/* --- memory management --- */
typedef struct _ProtobufCAllocator ProtobufCAllocator;
struct _ProtobufCAllocator
{
  void *(*alloc)(void *allocator_data, size_t size);
  void (*free)(void *allocator_data, void *pointer);
  void *(*tmp_alloc)(void *allocator_data, size_t size);
  unsigned max_alloca;
  void *allocator_data;
};
extern ProtobufCAllocator protobuf_c_default_allocator; /* settable */
extern ProtobufCAllocator protobuf_c_system_allocator;  /* use malloc, free etc */

extern void (*protobuf_c_out_of_memory) (void);

/* --- append-only data buffer --- */
typedef struct _ProtobufCBuffer ProtobufCBuffer;
struct _ProtobufCBuffer
{
  void (*append)(ProtobufCBuffer     *buffer,
                 size_t               len,
                 const unsigned char *data);
};
/* --- enums --- */
typedef struct _ProtobufCEnumValue ProtobufCEnumValue;
typedef struct _ProtobufCEnumDescriptor ProtobufCEnumDescriptor;

struct _ProtobufCEnumValue
{
  const char *name;
  const char *c_name;
  int value;
};

struct _ProtobufCEnumDescriptor
{
  const char *name;
  const char *short_name;
  const char *c_name;
  const char *package_name;

  /* sorted by value */
  unsigned n_values;
  const ProtobufCEnumValue *values;

  /* sorted by name */
  unsigned n_value_names;
  const ProtobufCEnumValue *values_by_name;
};

/* --- messages --- */
typedef struct _ProtobufCMessageDescriptor ProtobufCMessageDescriptor;
typedef struct _ProtobufCFieldDescriptor ProtobufCFieldDescriptor;
struct _ProtobufCFieldDescriptor
{
  const char *name;
  int id;
  ProtobufCLabel label;
  ProtobufCType type;
  unsigned quantifier_offset;
  unsigned offset;
  const void *descriptor;   /* for MESSAGE and ENUM types */
};
struct _ProtobufCMessageDescriptor
{
  const char *name;
  const char *short_name;
  const char *c_name;
  const char *package_name;

  size_t sizeof_message;

  /* sorted by field-id */
  unsigned n_fields;
  const ProtobufCFieldDescriptor *fields;

  /* ranges, optimization for looking up fields */
  unsigned n_field_ranges;
  const ProtobufCIntRange *field_ranges;

  /* offset in message to the array of unknown fields */
  unsigned unknown_field_array_offset;
};

typedef struct _ProtobufCMessage ProtobufCMessage;
struct _ProtobufCMessage
{
  const ProtobufCMessageDescriptor *descriptor;
};

size_t    protobuf_c_message_get_packed_size(const ProtobufCMessage *message);
size_t    protobuf_c_message_pack           (const ProtobufCMessage *message,
                                             unsigned char *out);
size_t    protobuf_c_message_pack_to_buffer (const ProtobufCMessage *message,
                                             ProtobufCBuffer  *buffer);

ProtobufCMessage *
          protobuf_c_message_unpack         (const ProtobufCMessageDescriptor *,
                                             ProtobufCAllocator  *allocator,
                                             size_t               len,
                                             const unsigned char *data);
void      protobuf_c_message_free_unpacked  (ProtobufCMessage    *message,
                                             ProtobufCAllocator  *allocator);


/* --- services --- */
typedef struct _ProtobufCMethodDescriptor ProtobufCMethodDescriptor;
typedef struct _ProtobufCServiceDescriptor ProtobufCServiceDescriptor;

struct _ProtobufCMethodDescriptor
{
  const char *name;
  const ProtobufCMessageDescriptor *input;
  const ProtobufCMessageDescriptor *output;
};
struct _ProtobufCServiceDescriptor
{
  const char *name;
  const char *short_name;
  const char *c_name;
  const char *package;
  unsigned n_methods;
  const ProtobufCMethodDescriptor *methods;		// sorted by name
};

typedef struct _ProtobufCService ProtobufCService;
typedef void (*ProtobufCClosure)(const ProtobufCMessage *message,
                                 void                   *closure_data);
struct _ProtobufCService
{
  const ProtobufCServiceDescriptor *descriptor;
  void (*invoke)(ProtobufCService *service,
                 unsigned          method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure  closure,
                 void             *closure_data);
  void (*destroy) (ProtobufCService *service);
};

ProtobufCService *protobuf_c_create_service_from_vfuncs
                               (const ProtobufCServiceDescriptor *descriptor,
                                void *service);
void protobuf_c_service_destroy (ProtobufCService *);


/* --- wire format enums --- */
typedef enum
{
  PROTOBUF_C_WIRE_TYPE_VARINT,
  PROTOBUF_C_WIRE_TYPE_64BIT,
  PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED,
  PROTOBUF_C_WIRE_TYPE_START_GROUP,     /* unsupported */
  PROTOBUF_C_WIRE_TYPE_END_GROUP,       /* unsupported */
  PROTOBUF_C_WIRE_TYPE_32BIT
} ProtobufCWireType;

/* --- unknown message fields --- */
typedef struct _ProtobufCMessageUnknownField ProtobufCMessageUnknownField;
typedef struct _ProtobufCMessageUnknownFieldArray ProtobufCMessageUnknownFieldArray;
struct _ProtobufCMessageUnknownField
{
  uint32_t tag;
  ProtobufCWireType wire_type;
  size_t len;
  unsigned char *data;
};
struct _ProtobufCMessageUnknownFieldArray
{
  unsigned n_unknown_fields;
  ProtobufCMessageUnknownField *unknown_fields;
};

/* --- extra (superfluous) api:  trivial buffer --- */
typedef struct _ProtobufCBufferSimple ProtobufCBufferSimple;
struct _ProtobufCBufferSimple
{
  ProtobufCBuffer base;
  size_t alloced;
  size_t len;
  unsigned char *data;
  protobuf_c_boolean must_free_data;
};
#define PROTOBUF_C_BUFFER_SIMPLE_INIT(array_of_bytes) \
{ { protobuf_c_buffer_simple_append }, \
  sizeof(array_of_bytes), 0, (array_of_bytes), 0 }
#define PROTOBUF_C_BUFFER_SIMPLE_CLEAR(simp_buf) \
  do { if ((simp_buf)->must_free_data) \
         protobuf_c_default_allocator.free (&protobuf_c_default_allocator.allocator_data, (simp_buf)->data); } while (0)

/* ====== private ====== */
#include "protobuf-c-private.h"
#endif /* __PROTOBUF_C_RUNTIME_H_ */
