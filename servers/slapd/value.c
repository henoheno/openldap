/* value.c - routines for dealing with values */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include <sys/stat.h>

#include "slap.h"

int
value_add( 
    struct berval	***vals,
    struct berval	**addvals
)
{
	int	n, nn, i, j;

	for ( nn = 0; addvals != NULL && addvals[nn] != NULL; nn++ )
		;	/* NULL */

	if ( *vals == NULL ) {
		*vals = (struct berval **) ch_malloc( (nn + 1)
		    * sizeof(struct berval *) );
		n = 0;
	} else {
		for ( n = 0; (*vals)[n] != NULL; n++ )
			;	/* NULL */
		*vals = (struct berval **) ch_realloc( (char *) *vals,
		    (n + nn + 1) * sizeof(struct berval *) );
	}

	for ( i = 0, j = 0; i < nn; i++ ) {
		if ( addvals[i]->bv_len > 0 ) {
			(*vals)[n + j] = ber_bvdup( addvals[i] );
			if( (*vals)[n + j++] == NULL ) break;
		}
	}
	(*vals)[n + j] = NULL;

	return LDAP_SUCCESS;
}

#ifdef SLAPD_SCHEMA_NOT_COMPAT
	/* not used */
#else
int
value_add_fast( 
    struct berval	***vals,
    struct berval	**addvals,
    int			nvals,
    int			naddvals,
    int			*maxvals
)
{
	int	need, i, j;

	if ( *maxvals == 0 ) {
		*maxvals = 1;
	}
	need = nvals + naddvals + 1;
	while ( *maxvals < need ) {
		*maxvals *= 2;
		*vals = (struct berval **) ch_realloc( (char *) *vals,
		    *maxvals * sizeof(struct berval *) );
	}

	for ( i = 0, j = 0; i < naddvals; i++ ) {
		if ( addvals[i]->bv_len > 0 ) {
			(*vals)[nvals + j] = ber_bvdup( addvals[i] );
			if( (*vals)[nvals + j] != NULL ) j++;
		}
	}
	(*vals)[nvals + j] = NULL;

	return( 0 );
}
#endif

#ifdef SLAPD_SCHEMA_NOT_COMPAT
int
value_normalize(
	AttributeDescription *ad,
	unsigned usage,
	struct berval *in,
	struct berval **out,
	const char **text )
{
	int rc;
	MatchingRule *mr;

	switch( usage & SLAP_MR_TYPE_MASK ) {
	case SLAP_MR_NONE:
	case SLAP_MR_EQUALITY:
		mr = ad->ad_type->sat_equality;
		break;
	case SLAP_MR_ORDERING:
		mr = ad->ad_type->sat_ordering;
		break;
	case SLAP_MR_SUBSTR:
		mr = ad->ad_type->sat_substr;
		break;
	case SLAP_MR_EXT:
	default:
		assert( 0 );
		*text = "internal error";
		return LDAP_OTHER;
	}

	if( mr == NULL ) {
		*text = "inappropriate matching request";
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	/* we only support equality matching of binary attributes */
	if( slap_ad_is_binary( ad ) && usage != SLAP_MR_EQUALITY ) {
		*text = "inappropriate binary matching";
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	if( mr->smr_normalize ) {
		rc = (mr->smr_normalize)( usage,
			ad->ad_type->sat_syntax,
			mr, in, out );

		if( rc != LDAP_SUCCESS ) {
			*text = "unable to normalize value";
			return LDAP_INVALID_SYNTAX;
		}

	} else if ( mr->smr_syntax->ssyn_normalize ) {
		rc = (mr->smr_syntax->ssyn_normalize)(
			ad->ad_type->sat_syntax,
			in, out );

		if( rc != LDAP_SUCCESS ) {
			*text = "unable to normalize value";
			return LDAP_INVALID_SYNTAX;
		}

	} else {
		*out = ber_bvdup( in );
	}

	return LDAP_SUCCESS;
}

#else
void
value_normalize(
    char	*s,
    int		syntax
)
{
	char	*d, *save;

	if ( ! (syntax & SYNTAX_CIS) ) {
		return;
	}

	if ( syntax & SYNTAX_DN ) {
		(void) dn_normalize( s );
		return;
	}

	save = s;
	for ( d = s; *s; s++ ) {
		if ( (syntax & SYNTAX_TEL) && (*s == ' ' || *s == '-') ) {
			continue;
		}
		*d++ = TOUPPER( (unsigned char) *s );
	}
	*d = '\0';
}
#endif

#ifdef SLAPD_SCHEMA_NOT_COMPAT
int
value_match(
	int *match,
	AttributeDescription *ad,
	MatchingRule *mr,
	struct berval *v1, /* stored value */
	void *v2, /* assertion */
	const char ** text )
{
	int rc;
	int usage = 0;
	struct berval *nv1 = NULL;

	if( !mr->smr_match ) {
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	if( ad->ad_type->sat_syntax->ssyn_normalize ) {
		rc = ad->ad_type->sat_syntax->ssyn_normalize(
			ad->ad_type->sat_syntax, v1, &nv1 );

		if( rc != LDAP_SUCCESS ) {
			return LDAP_INAPPROPRIATE_MATCHING;
		}
	}

	rc = (mr->smr_match)( match, usage,
		ad->ad_type->sat_syntax,
		mr,
		nv1 != NULL ? nv1 : v1,
		v2 );
	
	ber_bvfree( nv1 );
	return rc;
}

#else
int
value_cmp(
    struct berval	*v1,
    struct berval	*v2,
    int			syntax,
    int			normalize	/* 1 => arg 1; 2 => arg 2; 3 => both */
)
{
	int		rc;

	if ( normalize & 1 ) {
		v1 = ber_bvdup( v1 );
		value_normalize( v1->bv_val, syntax );
	}
	if ( normalize & 2 ) {
		v2 = ber_bvdup( v2 );
		value_normalize( v2->bv_val, syntax );
	}

	switch ( syntax ) {
	case SYNTAX_CIS:
	case (SYNTAX_CIS | SYNTAX_TEL):
	case (SYNTAX_CIS | SYNTAX_DN):
		rc = strcasecmp( v1->bv_val, v2->bv_val );
		break;

	case SYNTAX_CES:
		rc = strcmp( v1->bv_val, v2->bv_val );
		break;

	default:        /* Unknown syntax */
	case SYNTAX_BIN:
		rc = (v1->bv_len == v2->bv_len
		      ? memcmp( v1->bv_val, v2->bv_val, v1->bv_len )
		      : v1->bv_len > v2->bv_len ? 1 : -1);
		break;
	}

	if ( normalize & 1 ) {
		ber_bvfree( v1 );
	}
	if ( normalize & 2 ) {
		ber_bvfree( v2 );
	}

	return( rc );
}
#endif

#ifdef SLAPD_SCHEMA_NOT_COMPAT
int value_find(
	AttributeDescription *ad,
	struct berval **vals,
	struct berval *val )
#else
int
value_find(
    struct berval	**vals,
    struct berval	*v,
    int			syntax,
    int			normalize )
#endif
{
	int	i;
#ifdef SLAPD_SCHEMA_NOT_COMPAT
	int rc;
	struct berval *nval = NULL;
	MatchingRule *mr = ad->ad_type->sat_equality;

	if( mr == NULL || !mr->smr_match ) {
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	if( mr->smr_syntax->ssyn_normalize ) {
		rc = mr->smr_syntax->ssyn_normalize(
			mr->smr_syntax, val, &nval );

		if( rc != LDAP_SUCCESS ) {
			return LDAP_INAPPROPRIATE_MATCHING;
		}
	}
#endif

	for ( i = 0; vals[i] != NULL; i++ ) {
#ifdef SLAPD_SCHEMA_NOT_COMPAT
		int match;
		const char *text;

		rc = value_match( &match, ad, mr, vals[i],
			nval == NULL ? val : nval, &text );

		if( rc == LDAP_SUCCESS && match == 0 )
#else
		if ( value_cmp( vals[i], v, syntax, normalize ) == 0 )
#endif
		{
			return LDAP_SUCCESS;
		}
	}

	return LDAP_NO_SUCH_ATTRIBUTE;
}
