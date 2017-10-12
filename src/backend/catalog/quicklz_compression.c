/*---------------------------------------------------------------------
 *
 * quicklz_compression.c
 *
 * The quicklz implementation is not provided due to licensing issues.
 * The following stub implementation is built if a proprietary
 * implementation is not provided.
 *
 * Note that other compression algorithms actually work better for new
 * installations anyway.
 *
 * Portions Copyright (c) 2011 EMC Corporation All Rights Reserved
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/backend/catalog/quicklz_compression.c
 *
 *---------------------------------------------------------------------
 */

#include "postgres.h"
#include "utils/builtins.h"
#include "lz4.h"
#include "catalog/pg_compression.h"
#include <stddef.h>

size_t
dsz(size_t input){
  return LZ4_COMPRESSBOUND(input);
}

Datum
quicklz_constructor(PG_FUNCTION_ARGS)
{

  /* PG_GETARG_POINTER(0) is TupleDesc that is currently unused.
   * It is passed as NULL */

  StorageAttributes *sa = PG_GETARG_POINTER(1);
  CompressionState *cs	   = palloc0(sizeof(CompressionState));
  bool		   compress = PG_GETARG_BOOL(2);

  cs->desired_sz = dsz;
  /*if (compress) {
      cs->desired_sz = dsz;
   } else {
      cs->desired_sz = NULL;
   }*/

  Insist(PointerIsValid(sa->comptype));

  if (sa->complevel == 0)
          sa->complevel = 1;

  PG_RETURN_POINTER(cs);
}

Datum
quicklz_destructor(PG_FUNCTION_ARGS)
{
  PG_RETURN_VOID();
}

Datum
quicklz_compress(PG_FUNCTION_ARGS)
{
  const void	   *src	  = PG_GETARG_POINTER(0);
  int32			 src_sz   = PG_GETARG_INT32(1);
  void			 *dst	  = PG_GETARG_POINTER(2);
  int32			 dst_sz   = PG_GETARG_INT32(3);
  int32			*dst_used = PG_GETARG_POINTER(4);

  *dst_used = LZ4_compress_default((char *) src, (char *) dst, src_sz, dst_sz);


  if (*dst_used <= 0)
  {
      elog(ERROR, "lz4 compress failed");
  }

  PG_RETURN_VOID();
}

Datum
quicklz_decompress(PG_FUNCTION_ARGS)
{
  const char	   *src	= PG_GETARG_POINTER(0);
  int32			src_sz = PG_GETARG_INT32(1);
  void		   *dst	= PG_GETARG_POINTER(2);
  int32			dst_sz = PG_GETARG_INT32(3);
  int32		   *dst_used = PG_GETARG_POINTER(4);

  Insist(src_sz > 0 && dst_sz > 0);

  *dst_used = LZ4_decompress_safe(src, (char *)dst, src_sz, dst_sz);

  if (*dst_used <= 0)
  {
      elog(ERROR, "lz4 decompress failed");
  }

  PG_RETURN_VOID();
}

Datum
quicklz_validator(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


