/* $PostgreSQL: pgsql/contrib/citext/citext.sql.in,v 1.2 2008/07/30 17:08:52 tgl Exp $ */

-- Adjust this setting to control where the objects get created.
SET search_path = public;

--
--  PostgreSQL code for CITEXT.
--
-- Most I/O functions, and a few others, piggyback on the "text" type
-- functions via the implicit cast to text.
--

--
-- Shell type to keep things a bit quieter.
--

CREATE TYPE citext;

--
--  Input and output functions.
--
CREATE OR REPLACE FUNCTION citextin(cstring)
RETURNS citext
AS 'textin'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citextout(citext)
RETURNS cstring
AS 'textout'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citextrecv(internal)
RETURNS citext
AS 'textrecv'
LANGUAGE 'internal' STABLE STRICT;

CREATE OR REPLACE FUNCTION citextsend(citext)
RETURNS bytea
AS 'textsend'
LANGUAGE 'internal' STABLE STRICT;

--
--  The type itself.
--

CREATE TYPE citext (
    INPUT          = citextin,
    OUTPUT         = citextout,
    RECEIVE        = citextrecv,
    SEND           = citextsend,
    INTERNALLENGTH = VARIABLE,
    STORAGE        = extended
);

--
-- A single cast function, since bpchar needs to have its whitespace trimmed
-- before it's cast to citext.
--
CREATE OR REPLACE FUNCTION citext(bpchar)
RETURNS citext
AS 'rtrim1'
LANGUAGE 'internal' IMMUTABLE STRICT;

--
--  Implicit and assignment type casts.
--

CREATE CAST (citext AS text)    WITHOUT FUNCTION AS IMPLICIT;
CREATE CAST (citext AS varchar) WITHOUT FUNCTION AS IMPLICIT;
CREATE CAST (citext AS bpchar)  WITHOUT FUNCTION AS ASSIGNMENT;
CREATE CAST (text AS citext)    WITHOUT FUNCTION AS ASSIGNMENT;
CREATE CAST (varchar AS citext) WITHOUT FUNCTION AS ASSIGNMENT;
CREATE CAST (bpchar AS citext)  WITH FUNCTION citext(bpchar) AS ASSIGNMENT;

--
-- Operator Functions.
--

CREATE OR REPLACE FUNCTION citext_eq( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_ne( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_lt( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_le( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_gt( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_ge( citext, citext )
RETURNS bool
AS '$libdir/citext'
LANGUAGE C IMMUTABLE STRICT;

--
-- Operators.
--

CREATE OPERATOR = (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    COMMUTATOR = =,
    NEGATOR    = <>,
    PROCEDURE  = citext_eq,
    RESTRICT   = eqsel,
    JOIN       = eqjoinsel,
    HASHES,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    NEGATOR    = =,
    COMMUTATOR = <>,
    PROCEDURE  = citext_ne,
    RESTRICT   = neqsel,
    JOIN       = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    NEGATOR    = >=,
    COMMUTATOR = >,
    PROCEDURE  = citext_lt,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    NEGATOR    = >,
    COMMUTATOR = >=,
    PROCEDURE  = citext_le,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR >= (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    NEGATOR    = <,
    COMMUTATOR = <=,
    PROCEDURE  = citext_ge,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR > (
    LEFTARG    = CITEXT,
    RIGHTARG   = CITEXT,
    NEGATOR    = <=,
    COMMUTATOR = <,
    PROCEDURE  = citext_gt,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

--
-- Support functions for indexing.
--

CREATE OR REPLACE FUNCTION citext_cmp(citext, citext)
RETURNS int4
AS '$libdir/citext'
LANGUAGE C STRICT IMMUTABLE;

CREATE OR REPLACE FUNCTION citext_hash(citext)
RETURNS int4
AS '$libdir/citext'
LANGUAGE C STRICT IMMUTABLE;

--
-- The btree indexing operator class.
--

CREATE OPERATOR CLASS citext_ops
DEFAULT FOR TYPE CITEXT USING btree AS
    OPERATOR    1   <  (citext, citext),
    OPERATOR    2   <= (citext, citext),
    OPERATOR    3   =  (citext, citext),
    OPERATOR    4   >= (citext, citext),
    OPERATOR    5   >  (citext, citext),
    FUNCTION    1   citext_cmp(citext, citext);

--
-- The hash indexing operator class.
--

CREATE OPERATOR CLASS citext_ops
DEFAULT FOR TYPE citext USING hash AS
    OPERATOR    1   =  (citext, citext),
    FUNCTION    1   citext_hash(citext);

--
-- Aggregates.
--

CREATE OR REPLACE FUNCTION citext_smaller(citext, citext)
RETURNS citext
AS '$libdir/citext'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION citext_larger(citext, citext)
RETURNS citext
AS '$libdir/citext'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE AGGREGATE min(citext)  (
    SFUNC = citext_smaller,
    STYPE = citext,
    SORTOP = <
);

CREATE AGGREGATE max(citext)  (
    SFUNC = citext_larger,
    STYPE = citext,
    SORTOP = >
);

--
-- CITEXT pattern matching.
--

CREATE OR REPLACE FUNCTION texticlike(citext, citext)
RETURNS bool AS 'texticlike'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticnlike(citext, citext)
RETURNS bool AS 'texticnlike'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticregexeq(citext, citext)
RETURNS bool AS 'texticregexeq'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticregexne(citext, citext)
RETURNS bool AS 'texticregexne'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OPERATOR ~ (
    PROCEDURE = texticregexeq,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = !~,
    RESTRICT  = icregexeqsel,
    JOIN      = icregexeqjoinsel
);

CREATE OPERATOR ~* (
    PROCEDURE = texticregexeq,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = !~*,
    RESTRICT  = icregexeqsel,
    JOIN      = icregexeqjoinsel
);

CREATE OPERATOR !~ (
    PROCEDURE = texticregexne,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = ~,
    RESTRICT  = icregexnesel,
    JOIN      = icregexnejoinsel
);

CREATE OPERATOR !~* (
    PROCEDURE = texticregexne,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = ~*,
    RESTRICT  = icregexnesel,
    JOIN      = icregexnejoinsel
);

CREATE OPERATOR ~~ (
    PROCEDURE = texticlike,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = !~~,
    RESTRICT  = iclikesel,
    JOIN      = iclikejoinsel
);

CREATE OPERATOR ~~* (
    PROCEDURE = texticlike,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = !~~*,
    RESTRICT  = iclikesel,
    JOIN      = iclikejoinsel
);

CREATE OPERATOR !~~ (
    PROCEDURE = texticnlike,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = ~~,
    RESTRICT  = icnlikesel,
    JOIN      = icnlikejoinsel
);

CREATE OPERATOR !~~* (
    PROCEDURE = texticnlike,
    LEFTARG   = citext,
    RIGHTARG  = citext,
    NEGATOR   = ~~*,
    RESTRICT  = icnlikesel,
    JOIN      = icnlikejoinsel
);

--
-- Matching citext to text. 
--

CREATE OR REPLACE FUNCTION texticlike(citext, text)
RETURNS bool AS 'texticlike'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticnlike(citext, text)
RETURNS bool AS 'texticnlike'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticregexeq(citext, text)
RETURNS bool AS 'texticregexeq'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION texticregexne(citext, text)
RETURNS bool AS 'texticregexne'
LANGUAGE 'internal' IMMUTABLE STRICT;

CREATE OPERATOR ~ (
    PROCEDURE = texticregexeq,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = !~,
    RESTRICT  = icregexeqsel,
    JOIN      = icregexeqjoinsel
);

CREATE OPERATOR ~* (
    PROCEDURE = texticregexeq,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = !~*,
    RESTRICT  = icregexeqsel,
    JOIN      = icregexeqjoinsel
);

CREATE OPERATOR !~ (
    PROCEDURE = texticregexne,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = ~,
    RESTRICT  = icregexnesel,
    JOIN      = icregexnejoinsel
);

CREATE OPERATOR !~* (
    PROCEDURE = texticregexne,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = ~*,
    RESTRICT  = icregexnesel,
    JOIN      = icregexnejoinsel
);

CREATE OPERATOR ~~ (
    PROCEDURE = texticlike,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = !~~,
    RESTRICT  = iclikesel,
    JOIN      = iclikejoinsel
);

CREATE OPERATOR ~~* (
    PROCEDURE = texticlike,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = !~~*,
    RESTRICT  = iclikesel,
    JOIN      = iclikejoinsel
);

CREATE OPERATOR !~~ (
    PROCEDURE = texticnlike,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = ~~,
    RESTRICT  = icnlikesel,
    JOIN      = icnlikejoinsel
);

CREATE OPERATOR !~~* (
    PROCEDURE = texticnlike,
    LEFTARG   = citext,
    RIGHTARG  = text,
    NEGATOR   = ~~*,
    RESTRICT  = icnlikesel,
    JOIN      = icnlikejoinsel
);
