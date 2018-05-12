/*
  Copyright 2011-2016 David Robillard <http://drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "node.h"

#include "base64.h"
#include "serd_internal.h"
#include "string_utils.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    ifndef isnan
#        define isnan(x) _isnan(x)
#    endif
#    ifndef isinf
#        define isinf(x) (!_finite(x))
#    endif
#endif

static const size_t serd_node_align = sizeof(SerdNode);

typedef struct StaticNode {
	SerdNode node;
	char     buf[sizeof(NS_XSD) + sizeof("base64Binary") + 1];
} StaticNode;

#define DEFINE_XSD_NODE(name) \
	static const StaticNode serd_xsd_ ## name = { \
		{ sizeof(NS_XSD #name) - 1, 0, SERD_URI }, NS_XSD #name };

DEFINE_XSD_NODE(decimal)
DEFINE_XSD_NODE(integer)
DEFINE_XSD_NODE(base64Binary)

static SerdNode*
serd_node_new_from_uri(const SerdURI* uri, const SerdURI* base);

static size_t
serd_node_pad_size(const size_t n_bytes)
{
	const size_t pad = serd_node_align - (n_bytes + 2) % serd_node_align;
	const size_t size = n_bytes + 2 + pad;
	assert(size % serd_node_align == 0);
	return size;
}

static const SerdNode*
serd_node_get_meta_c(const SerdNode* node)
{
	return node + 1 + (serd_node_pad_size(node->n_bytes) / serd_node_align);
}

static SerdNode*
serd_node_get_meta(SerdNode* node)
{
	return node + 1 + (serd_node_pad_size(node->n_bytes) / serd_node_align);
}

static size_t
serd_node_total_size(const SerdNode* node)
{
	const size_t len = sizeof(SerdNode) + serd_node_pad_size(node->n_bytes);
	if (node->flags & SERD_HAS_LANGUAGE) {
		return len + serd_node_total_size(serd_node_get_language(node));
	} else if (node->flags & SERD_HAS_DATATYPE) {
		return len + serd_node_total_size(serd_node_get_datatype(node));
	}
	return len;
}

SerdNode*
serd_node_malloc(size_t n_bytes, SerdNodeFlags flags, SerdType type)
{
	const size_t size = sizeof(SerdNode) + serd_node_pad_size(n_bytes);
	SerdNode*    node = (SerdNode*)calloc(1, size);
	node->n_bytes = 0;
	node->flags   = flags;
	node->type    = type;
	assert((intptr_t)node % serd_node_align == 0);
	return node;
}

void
serd_node_set(SerdNode** dst, const SerdNode* src)
{
	if (src) {
		const size_t size = serd_node_total_size(src);
		if (!(*dst) || serd_node_total_size(*dst) < size) {
			(*dst) = (SerdNode*)realloc(*dst, size);
		}

		memcpy(*dst, src, size);
	} else if (*dst) {
		(*dst)->type = SERD_NOTHING;
	}
}

static SerdNode*
serd_node_new_simple(SerdType type, const char* str)
{
	if (!str) {
		return NULL;
	}

	const size_t n_bytes = strlen(str);
	SerdNode*    node    = serd_node_malloc(n_bytes, 0, type);
	memcpy(serd_node_buffer(node), str, n_bytes);
	node->n_bytes = n_bytes;
	return node;
}

SerdNode*
serd_node_new_string(const char* str)
{
	if (!str) {
		return NULL;
	}

	uint32_t     flags   = 0;
	const size_t n_bytes = serd_strlen(str, &flags);
	SerdNode*    node    = serd_node_malloc(n_bytes, flags, SERD_LITERAL);
	memcpy(serd_node_buffer(node), str, n_bytes);
	node->n_bytes = n_bytes;
	return node;
}

SerdNode*
serd_node_new_literal(const char*     str,
                      const SerdNode* datatype,
                      const char*     lang)
{
	if (!str || (lang && datatype &&
	             strcmp(serd_node_buffer_c(datatype), NS_RDF "#langString")) ||
	    (datatype && serd_node_get_type(datatype) != SERD_URI)) {
		return NULL;
	}

	uint32_t     flags   = 0;
	const size_t n_bytes = serd_strlen(str, &flags);
	const size_t len     = serd_node_pad_size(n_bytes);

	SerdNode* node = NULL;
	if (lang) {
		flags |= SERD_HAS_LANGUAGE;
		const size_t lang_len  = strlen(lang);
		const size_t total_len = len + sizeof(SerdNode) + lang_len;
		node = serd_node_malloc(total_len, flags, SERD_LITERAL);
		memcpy(serd_node_buffer(node), str, n_bytes);
		node->n_bytes = n_bytes;

		SerdNode* lang_node = node + 1 + (len / serd_node_align);
		lang_node->type    = SERD_LITERAL;
		lang_node->n_bytes = lang_len;
		memcpy(serd_node_buffer(lang_node), lang, lang_len);
	} else if (datatype) {
		flags |= SERD_HAS_DATATYPE;
		const size_t datatype_len = strlen(serd_node_buffer_c(datatype));
		const size_t total_len    = len + sizeof(SerdNode) + datatype_len;
		node = serd_node_malloc(total_len, flags, SERD_LITERAL);
		memcpy(serd_node_buffer(node), str, n_bytes);
		node->n_bytes = n_bytes;

		SerdNode* datatype_node = node + 1 + (len / serd_node_align);
		memcpy(datatype_node, datatype, sizeof(SerdNode) + datatype_len);
	} else {
		node = serd_node_malloc(n_bytes, flags, SERD_LITERAL);
		memcpy(serd_node_buffer(node), str, n_bytes);
		node->n_bytes = n_bytes;
	}

	return node;
}

SerdNode*
serd_node_new_blank(const char* str)
{
	return serd_node_new_simple(SERD_BLANK, str);
}

SerdNode*
serd_node_new_curie(const char* str)
{
	return serd_node_new_simple(SERD_CURIE, str);
}

SerdNode*
serd_node_new_uri(const char* str)
{
	return serd_node_new_simple(SERD_URI, str);
}

SerdNode*
serd_node_copy(const SerdNode* node)
{
	if (!node) {
		return NULL;
	}

	const size_t size = serd_node_total_size(node);
	SerdNode*    copy = (SerdNode*)calloc(1, size + 3);
	memcpy(copy, node, size);
	return copy;
}

bool
serd_node_equals(const SerdNode* a, const SerdNode* b)
{
	if (a == b) {
		return true;
	} else if (!a || !b) {
		return false;
	}

	const size_t a_size = serd_node_total_size(a);
	if (serd_node_total_size(b) == a_size) {
		return !memcmp(a, b, a_size);
	}
	return false;
}

static size_t
serd_uri_string_length(const SerdURI* uri)
{
	size_t len = uri->path_base.len;

#define ADD_LEN(field, n_delims) \
	if ((field).len) { len += (field).len + (n_delims); }

	ADD_LEN(uri->path,      1);  // + possible leading `/'
	ADD_LEN(uri->scheme,    1);  // + trailing `:'
	ADD_LEN(uri->authority, 2);  // + leading `//'
	ADD_LEN(uri->query,     1);  // + leading `?'
	ADD_LEN(uri->fragment,  1);  // + leading `#'

	return len + 2;  // + 2 for authority `//'
}

static size_t
string_sink(const void* buf, size_t size, size_t nmemb, void* stream)
{
	char** ptr = (char**)stream;
	memcpy(*ptr, buf, size * nmemb);
	*ptr += size * nmemb;
	return nmemb;
}

SerdNode*
serd_node_new_resolved_uri(const char* str, const SerdNode* base)
{
	if (!base || base->type != SERD_URI) {
		return NULL;
	}

	SerdURI base_uri;
	serd_uri_parse(serd_node_get_string(base), &base_uri);
	return serd_node_new_resolved_uri_i(str, &base_uri);
}

SerdNode*
serd_node_resolve(const SerdNode* node, const SerdNode* base)
{
	if (!node || !base || node->type != SERD_URI || base->type != SERD_URI) {
		return NULL;
	}

	SerdURI uri;
	SerdURI base_uri;
	serd_uri_parse(serd_node_get_string(node), &uri);
	serd_uri_parse(serd_node_get_string(base), &base_uri);

	return serd_node_new_from_uri(&uri, &base_uri);
}

SerdNode*
serd_node_new_resolved_uri_i(const char* str, const SerdURI* base)
{
	if (!str || str[0] == '\0') {
		// Empty URI => Base URI, or nothing if no base is given
		return base ? serd_node_new_from_uri(base, NULL) : NULL;
	}

	SerdURI uri;
	serd_uri_parse(str, &uri);
	return serd_node_new_from_uri(&uri, base);
}

static inline bool
is_uri_path_char(const char c)
{
	if (is_alpha(c) || is_digit(c)) {
		return true;
	}
	switch (c) {
	case '-': case '.': case '_': case '~':	 // unreserved
	case ':': case '@':	 // pchar
	case '/':  // separator
	// sub-delims
	case '!': case '$': case '&': case '\'': case '(': case ')':
	case '*': case '+': case ',': case ';': case '=':
		return true;
	default:
		return false;
	}
}

SerdNode*
serd_node_new_file_uri(const char* path, const char* hostname, bool escape)
{
	const size_t path_len     = strlen(path);
	const size_t hostname_len = hostname ? strlen(hostname) : 0;
	const bool   evil         = is_windows_path(path);
	size_t       uri_len      = 0;
	char*        uri          = NULL;

	if (path[0] == '/' || is_windows_path(path)) {
		uri_len = strlen("file://") + hostname_len + evil;
		uri = (char*)malloc(uri_len + 1);
		snprintf(uri, uri_len + 1, "file://%s%s",
		         hostname ? hostname : "", evil ? "/" : "");
	}

	SerdBuffer buffer = { uri, uri_len };
	for (size_t i = 0; i < path_len; ++i) {
		if (evil && path[i] == '\\') {
			serd_buffer_sink("/", 1, 1, &buffer);
		} else if (path[i] == '%') {
			serd_buffer_sink("%%", 1, 2, &buffer);
		} else if (!escape || is_uri_path_char(path[i])) {
			serd_buffer_sink(path + i, 1, 1, &buffer);
		} else {
			char escape_str[4] = { '%', 0, 0, 0 };
			snprintf(escape_str + 1, sizeof(escape_str) - 1, "%X", path[i]);
			serd_buffer_sink(escape_str, 1, 3, &buffer);
		}
	}
	serd_buffer_sink_finish(&buffer);

	SerdNode* node = serd_node_new_uri((const char*)buffer.buf);
	free(buffer.buf);
	return node;
}

static SerdNode*
serd_node_new_from_uri(const SerdURI* uri, const SerdURI* base)
{
	SerdURI abs_uri = *uri;
	if (base) {
		serd_uri_resolve(uri, base, &abs_uri);
	}

	const size_t len        = serd_uri_string_length(&abs_uri);
	SerdNode*    node       = serd_node_malloc(len, 0, SERD_URI);
	char*        ptr        = serd_node_buffer(node);
	const size_t actual_len = serd_uri_serialise(&abs_uri, string_sink, &ptr);

	serd_node_buffer(node)[actual_len] = '\0';
	node->n_bytes = actual_len;

	return node;
}

SerdNode*
serd_node_new_relative_uri(const char*     str,
                           const SerdNode* base,
                           const SerdNode* root)
{
	SerdURI uri      = SERD_URI_NULL;
	SerdURI base_uri = SERD_URI_NULL;
	SerdURI root_uri = SERD_URI_NULL;

	serd_uri_parse(str, &uri);
	if (base) {
		serd_uri_parse(serd_node_get_string(base), &base_uri);
	}
	if (root) {
		serd_uri_parse(serd_node_get_string(root), &root_uri);
	}

	const size_t uri_len    = serd_uri_string_length(&uri);
	const size_t base_len   = serd_uri_string_length(&base_uri);
	SerdNode*    node       = serd_node_malloc(uri_len + base_len, 0, SERD_URI);
	char*        ptr        = serd_node_buffer(node);
	const size_t actual_len = serd_uri_serialise_relative(
		&uri, &base_uri, root ? &root_uri : NULL, string_sink, &ptr);

	serd_node_buffer(node)[actual_len] = '\0';
	node->n_bytes = actual_len;

	return node;
}

static inline unsigned
serd_digits(double abs)
{
	const double lg = ceil(log10(floor(abs) + 1.0));
	return lg < 1.0 ? 1U : (unsigned)lg;
}

SerdNode*
serd_node_new_decimal(double d, unsigned frac_digits, const SerdNode* datatype)
{
	if (isnan(d) || isinf(d)) {
		return NULL;
	}

	const SerdNode* type       = datatype ? datatype : &serd_xsd_decimal.node;
	const double    abs_d      = fabs(d);
	const unsigned  int_digits = serd_digits(abs_d);
	const size_t    len        = int_digits + frac_digits + 3;
	const size_t    type_len   = serd_node_total_size(type);
	const size_t    total_len  = len + type_len;

	SerdNode* const node =
		serd_node_malloc(total_len, SERD_HAS_DATATYPE, SERD_LITERAL);

	// Point s to decimal point location
	char* const  buf      = serd_node_buffer(node);
	const double int_part = floor(abs_d);
	char*        s        = buf + int_digits;
	if (d < 0.0) {
		*buf = '-';
		++s;
	}

	// Write integer part (right to left)
	char*    t   = s - 1;
	uint64_t dec = (uint64_t)int_part;
	do {
		*t-- = '0' + (dec % 10);
	} while ((dec /= 10) > 0);


	*s++ = '.';

	// Write fractional part (right to left)
	double frac_part = fabs(d - int_part);
	if (frac_part < DBL_EPSILON) {
		*s++ = '0';
		node->n_bytes = (s - buf);
	} else {
		uint64_t frac = llround(frac_part * pow(10.0, (int)frac_digits));
		s += frac_digits - 1;
		unsigned i = 0;

		// Skip trailing zeros
		for (; i < frac_digits - 1 && !(frac % 10); ++i, --s, frac /= 10) {}

		node->n_bytes = (s - buf) + 1;

		// Write digits from last trailing zero to decimal point
		for (; i < frac_digits; ++i) {
			*s-- = '0' + (frac % 10);
			frac /= 10;
		}
	}

	memcpy(serd_node_get_meta(node), type, type_len);
	return node;
}

SerdNode*
serd_node_new_integer(int64_t i, const SerdNode* datatype)
{
	const SerdNode* type      = datatype ? datatype : &serd_xsd_integer.node;
	int64_t         abs_i     = (i < 0) ? -i : i;
	const unsigned  digits    = serd_digits(abs_i);
	const size_t    type_len  = serd_node_total_size(type);
	const size_t    total_len = digits + 2 + type_len;

	SerdNode* node =
		serd_node_malloc(total_len, SERD_HAS_DATATYPE, SERD_LITERAL);

	// Point s to the end
	char* buf = serd_node_buffer(node);
	char* s   = buf + digits - 1;
	if (i < 0) {
		*buf = '-';
		++s;
	}

	node->n_bytes = (s - buf) + 1;

	// Write integer part (right to left)
	do {
		*s-- = '0' + (abs_i % 10);
	} while ((abs_i /= 10) > 0);

	memcpy(serd_node_get_meta(node), type, type_len);
	return node;
}

SerdNode*
serd_node_new_blob(const void*     buf,
                   size_t          size,
                   bool            wrap_lines,
                   const SerdNode* datatype)
{
	if (!buf || !size) {
		return NULL;
	}

	const SerdNode* type      = datatype ? datatype : &serd_xsd_base64Binary.node;
	const size_t    len       = serd_base64_get_length(size, wrap_lines);
	const size_t    type_len  = serd_node_total_size(type);
	const size_t    total_len = len + 1 + type_len;

	SerdNode* const node =
		serd_node_malloc(total_len, SERD_HAS_DATATYPE, SERD_LITERAL);

	if (serd_base64_encode(serd_node_buffer(node), buf, size, wrap_lines)) {
		node->flags |= SERD_HAS_NEWLINE;
	}

	node->n_bytes = len;
	memcpy(serd_node_get_meta(node), type, type_len);
	return node;
}

SerdType
serd_node_get_type(const SerdNode* node)
{
	return node ? node->type : SERD_NOTHING;
}

const char*
serd_node_get_string(const SerdNode* node)
{
	return node ? (const char*)(node + 1) : NULL;
}

size_t
serd_node_get_length(const SerdNode* node)
{
	return node ? node->n_bytes : 0;
}

const SerdNode*
serd_node_get_datatype(const SerdNode* node)
{
	if (!node || !(node->flags & SERD_HAS_DATATYPE)) {
		return NULL;
	}

	const SerdNode* const datatype = serd_node_get_meta_c(node);
	assert(datatype->type == SERD_URI || datatype->type == SERD_CURIE);
	return datatype;
}

const SerdNode*
serd_node_get_language(const SerdNode* node)
{
	if (!node || !(node->flags & SERD_HAS_LANGUAGE)) {
		return NULL;
	}

	const SerdNode* const lang = serd_node_get_meta_c(node);
	assert(lang->type == SERD_LITERAL);
	return lang;
}

SerdNodeFlags
serd_node_get_flags(const SerdNode* node)
{
	return node->flags;
}

void
serd_node_free(SerdNode* node)
{
	free(node);
}
