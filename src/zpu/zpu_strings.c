/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

/* putc() is provided by zpu_fifo.c, the final printf call here needs to call
 * putc() to write to the actual ZPU FIFO */
#include "zpu_fifo.h"

/* This function is used from printf() to format int's to printable ASCII
 * Not intended to be called directly
 */
static void _printn(unsigned u, unsigned base, char issigned,
  volatile void (*emitter)(char, void *), void *pData)
{
	const char *_hex = "0123456789ABCDEF";
	if (issigned && ((int)u < 0)) {
		(*emitter)('-', pData);
		u = (unsigned)-((int)u);
	}
	if (u >= base) _printn(u/base, base, 0, emitter, pData);
	(*emitter)(_hex[u%base], pData);
}

/* A light-weight implementation of printf() that supports the common formats
 * Not intended to be called directly
 */
static void _printf(const char *format, volatile void (*emitter)(char, void *),
  void *pData, va_list va)
{
	char c;
	unsigned u;
	char *s;

	while (*format) {
		if (*format == '%') {
			switch (*++format) {
			  case 'c':
				c = (char)va_arg(va, int);
				(*emitter)(c, pData);
				break;
			  case 'u':
				u = va_arg(va, unsigned);
				_printn(u, 10, 0, emitter, pData);
				break;
			  case 'd':
				u = va_arg(va, unsigned);
				_printn(u, 10, 1, emitter, pData);
				break;
			  case 'x':
				u = va_arg(va, unsigned);
				_printn(u, 16, 0, emitter, pData);
				break;
			  case 's':
				s = va_arg(va, char *);
				while (*s) {
					(*emitter)(*s, pData);
					s++;
				}
			}
		} else {
			(*emitter)(*format, pData);
		}

		format++;
	}
}

/* Called from sprintf() to output string to a buffer.
 * Not intended to be called directly
 */
static void _buf_emitter(char c, void *pData)
{
	*((*((char **)pData)))++ = c;
}


/* Called from printf() to output each character to the FIFO.
 * Not intended to be called directly
 */
static void _char_emitter(char c, void *pData)
{
	putc(c);
}

/* The actual printf() function used that calls another function to do the real
 * formatting.
 * Can be called directly
 */
int printf(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	_printf(format, _char_emitter, NULL, va);
	return 0;
}

/* An implementation of sprintf() that uses the same printf() formatting func.
 * Can be called directly
 */
int sprintf(char *pInto, const char *format, ...)
{
	va_list va;
	char *pInto_orig = pInto;

	va_start(va, format);
	_printf(format, _buf_emitter, &pInto, va);
	*pInto = '\0';

	return pInto - pInto_orig;
}


