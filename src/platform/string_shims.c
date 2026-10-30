#include <stddef.h>

size_t strlen(const char* s) {
  const char* p = s;
  while (*p) ++p;
  return (size_t)(p - s);
}

char* strchr(const char* s, int c) {
  char ch = (char)c;
  while (*s) {
    if (*s == ch) return (char*)s;
    ++s;
  }
  return ch == 0 ? (char*)s : 0;
}

int strcmp(const char* first, const char* second) {
  while (*first && *first == *second) { ++first; ++second; }
  return (unsigned char)*first - (unsigned char)*second;
}

char* strcpy(char* destination, const char* source) {
  char* result = destination;
  while ((*destination++ = *source++)) {}
  return result;
}

void* memchr(const void* data, int c, size_t count) {
  const unsigned char* p = (const unsigned char*)data;
  const unsigned char value = (unsigned char)c;
  while (count--) { if (*p == value) return (void*)p; ++p; }
  return 0;
}

char* strncpy(char* destination, const char* source, size_t count) {
  size_t i = 0;
  for (; i < count && source[i]; ++i) destination[i] = source[i];
  for (; i < count; ++i) destination[i] = 0;
  return destination;
}

char* strcat(char* destination, const char* source) {
  char* end = destination;
  while (*end) ++end;
  while ((*end++ = *source++)) {}
  return destination;
}
