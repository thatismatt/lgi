/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implements marshalling, i.e. transferring values between Lua and GLib/C.
 */

#include "lgi.h"

/* Checks whether given argument contains number which fits given
   constraints. If yes, returns it, otehrwise throws Lua error. */
static lua_Number
check_number (lua_State *L, int narg, lua_Number val_min, lua_Number val_max)
{
  lua_Number val = luaL_checknumber (L, narg);
  if (val < val_min || val > val_max)
    {
      lua_pushfstring (L, "%f is out of <%f, %f>", val, val_min, val_max);
      luaL_argerror (L, narg, lua_tostring (L, -1));
    }
  return val;
}

/* Marshals integral types to C.  If requested, makes sure that the
   value is actually marshalled into val->v_pointer no matter what the
   input type is. */
static void
marshal_2c_int (lua_State *L, GITypeTag tag, GIArgument *val, int narg,
		gboolean optional, gboolean use_pointer)
{
  switch (tag)
    {
#define HANDLE_INT(nameupper, namelower, ptrconv, val_min, val_max)	\
    case GI_TYPE_TAG_ ## nameupper:					\
      val->v_ ## namelower = check_number (L, narg, val_min, val_max);	\
      if (use_pointer)						\
	val->v_pointer =						\
	  G ## ptrconv ## _TO_POINTER (val->v_ ## namelower);		\
      break

      HANDLE_INT(INT8, int8, INT, -0x80, 0x7f);
      HANDLE_INT(UINT8, uint8, UINT, 0, 0xff);
      HANDLE_INT(INT16, int16, INT, -0x8000, 0x7fff);
      HANDLE_INT(UINT16, uint16, UINT, 0, 0xffff);
      HANDLE_INT(INT32, int32, INT, -0x80000000L, 0x7fffffffL);
      HANDLE_INT(UINT32, uint32, UINT, 0, 0xffffffffL);
      HANDLE_INT(INT64, int64, INT, -0x8000000000000000LL, 
		 0x7fffffffffffffffLL);
      HANDLE_INT(UINT64, uint64, UINT, 0, 0xffffffffffffffffULL);
#if GLIB_SIZEOF_SIZE_T == 4
      HANDLE_INT(GTYPE, uint32, UINT, 0, 0xffffffffUL);
#else
      HANDLE_INT(GTYPE, uint64, UINT, 0, 0xffffffffffffffffULL);
#endif
#undef HANDLE_INT

    default:
      g_assert_not_reached ();
    }
}

/* Marshals integral types from C to Lua. */
static void
marshal_2lua_int (lua_State *L, GITypeTag tag, GIArgument *val,
		  gboolean use_pointer)
{
  switch (tag)
    {
#define HANDLE_INT(nameupper, namelower, ptrconv)	\
      case GI_TYPE_TAG_ ## nameupper:			\
	lua_pushnumber (L, use_pointer			\
	  ?  GPOINTER_TO_ ## ptrconv (val->v_pointer)	\
	  : val->v_ ## namelower);			\
	break;

      HANDLE_INT(INT8, int8, INT);
      HANDLE_INT(UINT8, uint8, UINT);
      HANDLE_INT(INT16, int16, INT);
      HANDLE_INT(UINT16, uint16, UINT);
      HANDLE_INT(INT32, int32, INT);
      HANDLE_INT(UINT32, uint32, UINT);
      HANDLE_INT(INT64, int64, INT);
      HANDLE_INT(UINT64, uint64, UINT);
#if GLIB_SIZEOF_SIZE_T == 4
      HANDLE_INT(GTYPE, uint32, UINT);
#else
      HANDLE_INT(GTYPE, uint64, UINT);
#endif
#undef HANDLE_INT

    default:
      g_assert_not_reached ();
    }
}

/* Gets or sets the length of the array. */
static void
array_get_or_set_length (GITypeInfo *ti, gssize *get_length, gssize set_length,
			 GICallableInfo *ci, void **args)
{
  gint param = g_type_info_get_array_length (ti);
  g_assert (ci != NULL);
  if (param >= 0 && param < g_callable_info_get_n_args (ci))
    {
      GIArgInfo ai;
      GITypeInfo eti;
      GIArgument *val;
      g_callable_info_load_arg (ci, param, &ai);
      g_arg_info_load_type (&ai, &eti);
      if (g_arg_info_get_direction (&ai) == GI_DIRECTION_IN)
	/* For input parameters, value is directly pointed do by args
	   table element. */
	val = (GIArgument *) args[param];
      else
	/* For output arguments, args table element points to pointer
	   to value. */
	val = *(GIArgument **) args[param];

      switch (g_type_info_get_tag (&eti))
	{
#define HANDLE_ELT(tag, field)			\
	  case GI_TYPE_TAG_ ## tag:		\
	    if (get_length != NULL)		\
	      *get_length = val->v_ ## field;	\
	    else				\
	      val->v_ ## field = set_length;	\
	  break

	  HANDLE_ELT(INT8, int8);
	  HANDLE_ELT(UINT8, uint8);
	  HANDLE_ELT(INT16, int16);
	  HANDLE_ELT(UINT16, uint16);
	  HANDLE_ELT(INT32, int32);
	  HANDLE_ELT(UINT32, uint32);
	  HANDLE_ELT(INT64, int64);
	  HANDLE_ELT(UINT64, uint64);
#undef HANDLE_ELT

	default:
	  g_assert_not_reached ();
	}
    }
}

/* Retrieves pointer to GIArgument in given array, given that array
   contains elements of type tag. */
static gssize
array_get_elt_size (GITypeTag tag)
{
  switch (tag)
    {
#define HANDLE_ELT(nameupper, nametype)		\
      case GI_TYPE_TAG_ ## nameupper:		\
	return sizeof (nametype);

      HANDLE_ELT(BOOLEAN, gboolean);
      HANDLE_ELT(INT8, gint8);
      HANDLE_ELT(UINT8, guint8);
      HANDLE_ELT(INT16, gint16);
      HANDLE_ELT(UINT16, guint16);
      HANDLE_ELT(INT32, gint32);
      HANDLE_ELT(UINT32, guint32);
      HANDLE_ELT(INT64, gint64);
      HANDLE_ELT(UINT64, guint64);
      HANDLE_ELT(FLOAT, gfloat);
      HANDLE_ELT(DOUBLE, gdouble);
      HANDLE_ELT(GTYPE, GType);
#undef HANDLE_ELT

    default:
      return sizeof (gpointer);
    }
}

/* Marshalls array from Lua to C. Returns number of temporary elements
   pushed to the stack. */
static int
marshal_2c_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		  GITransfer transfer, GIArgument *val, int narg,
		  gboolean optional, GICallableInfo *ci, void **args)
{
  GITypeInfo* eti;
  GITypeTag etag;
  gssize len, objlen, esize;
  gint index, vals = 0, to_pop, eti_guard;
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gboolean zero_terminated;
  GArray *array = NULL;

  /* Represent nil as NULL array. */
  if (optional && lua_isnoneornil (L, narg))
    val->v_pointer = NULL;
  {
    /* Check the type; we allow tables only. */
    luaL_checktype (L, narg, LUA_TTABLE);

    /* Get element type info, create guard for it. */
    eti = g_type_info_get_param_type (ti, 0);
    eti_guard = lgi_guard_create_baseinfo (L, eti);
    etag = g_type_info_get_tag (eti);
    esize = array_get_elt_size (etag);

    /* Find out how long array should we allocate. */
    zero_terminated = g_type_info_is_zero_terminated (ti);
    objlen = lua_objlen (L, narg);
    if (atype == GI_ARRAY_TYPE_ARRAY)
      len = objlen;
    else
      {
	len = g_type_info_get_array_fixed_size (ti);
	if (len >= 0 && objlen > len)
	  objlen = len;
      }

    /* Allocate the array and wrap it into the userdata guard, if needed. */
    if (len > 0 || zero_terminated)
      {
	array = g_array_sized_new (zero_terminated, TRUE, esize, len);
	g_array_set_size (array, len);
	if (transfer == GI_TRANSFER_NOTHING)
	  {
	    GArray **guard;
	    lgi_guard_create (L, (gpointer **) &guard,
			      (GDestroyNotify) g_array_unref);
	    *guard = array;
	    vals = 1;
	  }
      }

    /* Iterate through Lua array and fill GArray accordingly. */
    for (index = 0; index < objlen; index++)
      {
	lua_pushinteger (L, index + 1);
	lua_gettable (L, narg);

	/* Marshal element retrieved from the table into target array. */
	to_pop = lgi_marshal_2c (L, eti, NULL, exfer,
				 (GIArgument *) (array->data + index * esize),
				 -1, FALSE, NULL, NULL);

	/* Remove temporary element from the stack. */
	lua_remove (L, - to_pop - 1);

	/* Remember that some more temp elements could be pushed. */
	vals += to_pop;
      }
  }

  /* Fill in array length argument, if it is specified. */
  if (atype == GI_ARRAY_TYPE_C)
    array_get_or_set_length (ti, NULL, len, ci, args);

  /* Return either GArray or direct pointer to the data, according to
     the array type. */
  val->v_pointer = (atype == GI_ARRAY_TYPE_ARRAY || array == NULL)
    ? (void *) array : (void *) array->data;

  lua_remove (L, eti_guard);
  return vals;
}

static void
marshal_2lua_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		    GIArgument *val, GITransfer transfer, gboolean use_pointer,
		    GICallableInfo *ci, void **args)
{
  GITypeInfo* eti;
  GITypeTag etag;
  gssize len, esize;
  gint index, eti_guard;
  char *data;

  /* Get pointer to array data. */
  if (val->v_pointer == NULL)
    {
      /* NULL array is represented by nil. */
      lua_pushnil (L);
      return;
    }

  /* First of all, find out the length of the array. */
  if (atype == GI_ARRAY_TYPE_ARRAY)
    {
      len = ((GArray *) val->v_pointer)->len;
      data = ((GArray *) val->v_pointer)->data;
    }
  else
    {
      data = val->v_pointer;
      if (g_type_info_is_zero_terminated (ti))
	len = -1;
      else
	{
	  len = g_type_info_get_array_fixed_size (ti);
	  if (len == -1)
	    /* Length of the array is dynamic, get it from other
	       argument. */
	    array_get_or_set_length (ti, &len, 0, ci, args);
	}
    }

  /* Get array element type info, wrap it in the guard so that we
     don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  eti_guard = lgi_guard_create_baseinfo (L, eti);
  etag = g_type_info_get_tag (eti);
  esize = array_get_elt_size (etag);

  /* Create Lua table which will hold the array. */
  lua_createtable (L, len > 0 ? len : 0, 0);

  /* Iterate through array elements. */
  for (index = 0; len < 0 || index < len; index++)
    {
      /* Get value from specified index. */
      GIArgument *eval = (GIArgument *) (data + index * esize);

      /* If the array is zero-terminated, terminate now and don't
	 include NULL entry. */
      if (len < 0 && eval->v_pointer == NULL)
	break;

      /* Store value into the table. */
      lgi_marshal_2lua (L, eti, eval,
			(transfer == GI_TRANSFER_EVERYTHING) ?
			GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			FALSE, NULL, NULL);
      lua_rawseti (L, -2, index + 1);
    }

  /* If needed, free the array itself. */
  if (transfer != GI_TRANSFER_NOTHING)
    {
      if (atype == GI_ARRAY_TYPE_ARRAY)
	g_array_free (val->v_pointer, TRUE);
      else
	g_free (val->v_pointer);
    }

  lua_remove (L, eti_guard);
}

/* Marshalls GSList or GList from Lua to C. Returns number of
   temporary elements pushed to the stack. */
static int
marshal_2c_list (lua_State *L, GITypeInfo *ti, GITypeTag tag,
		 GITransfer transfer, GIArgument *val, int narg)
{
  GITypeInfo *eti;
  GITypeTag etag;
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gint index, vals = 0, to_pop, eti_guard;
  GSList **guard = NULL;

  /* Allow empty list to be expressed also as 'nil', because in C,
     there is no difference between NULL and empty list. */
  if (lua_isnoneornil (L, narg))
    index = 0;
  else
    {
      luaL_checktype (L, narg, LUA_TTABLE);
      index = lua_objlen (L, narg);
    }

  /* Get list element type info, create guard for it so that we don't
     leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  eti_guard = lgi_guard_create_baseinfo (L, eti);
  etag = g_type_info_get_tag (eti);

  /* Go from back and prepend to the list, which is cheaper than
     appending. */
  lgi_guard_create (L, (gpointer **) &guard, (GDestroyNotify) g_slist_free);
  while (index > 0)
    {
      /* Retrieve index-th element from the source table and marshall
	 it as pointer to arg. */
      GIArgument eval;
      lua_pushinteger (L, index--);
      lua_gettable (L, narg);
      to_pop = lgi_marshal_2c (L, eti, NULL, exfer, &eval, -1, TRUE,
			       NULL, NULL);

      /* Prepend new list element and reassign the guard. */
      if (tag == GI_TYPE_TAG_GSLIST)
	*guard = g_slist_prepend (*guard, eval.v_pointer);
      else
	*guard = (GSList *) g_list_prepend ((GList *) *guard, eval.v_pointer);

      lua_remove (L, - to_pop - 1);
      vals += to_pop;
    }

  /* Marshalled value is really kept inside the guard. */
  val->v_pointer = *guard;

  /* In case that the target takes ownership, destroy the guard, will
     not be really needed. */
  if (transfer == GI_TRANSFER_NOTHING)
    /* Keep guard alive, because target does not claim ownership. */
    vals = 1;
  else
    {
      /* Clear the guard, it is not needed. */
      *guard = NULL;
      lua_pop (L, 1);
      vals = 0;
    }

  lua_remove (L, eti_guard);
  return vals;
}

static int
marshal_2lua_list (lua_State *L, GITypeInfo *ti, GIArgument *val,
		   GITransfer xfer)
{
  GSList *list;
  GITypeInfo *eti;
  gint index, eti_guard;

  /* Get element type info, guard it so that we don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  eti_guard = lgi_guard_create_baseinfo (L, eti);

  /* Create table to which we will deserialize the list. */
  lua_newtable (L);

  /* Go through the list and push elements into the table. */
  for (list = (GSList *) val->v_pointer, index = 0; list != NULL;
       list = g_slist_next (list))
    {
      /* Get access to list item. */
      GIArgument *eval = (GIArgument *) &list->data;

      /* Store it into the table. */
      lgi_marshal_2lua (L, eti, eval,
			(xfer == GI_TRANSFER_EVERYTHING) ?
			GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			TRUE, NULL, NULL);
      lua_rawseti(L, -2, ++index);
    }

  /* Free the list, if requested. */
  if (xfer != GI_TRANSFER_NOTHING)
    g_slist_free (val->v_pointer);

  lua_remove (L, eti_guard);
  return 1;
}

/* Marshalls given callable from Lua to C. */
static int
marshal_2c_callable (lua_State *L, GICallableInfo *ci, GIArgInfo *ai,
		    GIArgument *val, int narg,
		    GICallableInfo *argci, void **args)
{
  int nret = 0;
  GIScopeType scope = g_arg_info_get_scope (ai);

  /* Create the closure. */
  gpointer closure = lgi_closure_create (L, ci, narg,
					 scope == GI_SCOPE_TYPE_ASYNC,
					 &val->v_pointer);

  /* Store user_data and/or destroy_notify arguments. */
  if (argci != NULL && args != NULL)
    {
      gint arg;
      gint nargs = g_callable_info_get_n_args (argci);
      arg = g_arg_info_get_closure (ai);
      if (arg >= 0 && arg < nargs)
	((GIArgument *) args[arg])->v_pointer = closure;
      arg = g_arg_info_get_destroy (ai);
      if (arg >= 0 && arg < nargs)
	((GIArgument *) args[arg])->v_pointer = lgi_closure_destroy;
    }

  /* In case of scope == SCOPE_TYPE_CALL, we have to create and store on the
     stack helper Lua userdata which destroy the closure in its gc. */
  if (scope == GI_SCOPE_TYPE_CALL)
    {
      lgi_closure_guard (L, closure);
      nret = 1;
    }

  return nret;
}

/* Marshalls single value from Lua to GLib/C. */
int
lgi_marshal_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		GITransfer transfer, GIArgument *val, int narg,
		gboolean use_pointer, GICallableInfo *ci, void **args)
{
  int nret = 0;
  gboolean optional = (ai != NULL && (g_arg_info_is_optional (ai) ||
				      g_arg_info_may_be_null (ai)));
  GITypeTag tag = g_type_info_get_tag (ti);
  switch (tag)
    {
    case GI_TYPE_TAG_BOOLEAN:
      if (!optional && lua_isnone (L, narg))
	luaL_typerror (L, narg, lua_typename (L, LUA_TBOOLEAN));
      val->v_boolean = lua_toboolean (L, narg) ? TRUE : FALSE;
      break;

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      {
	/* Retrieve number from given position. */
	lua_Number num = (optional && lua_isnoneornil (L, narg))
	  ? 0 : luaL_checknumber (L, narg);

	/* Decide where to store the number. */
	GIArgument *target;
	if (!use_pointer)
	  /* Marshal directly into val. */
	  target = val;
	else
	  {
	    /* Create temporary (inside userdata), and marshal to it
	       instead of using 'val' directly. */
	    target = val->v_pointer = lua_newuserdata (L, sizeof (GIArgument));
	    nret = 1;
	  }

	/* Store read value into chosen target. */
	if (tag == GI_TYPE_TAG_FLOAT)
	  target->v_float = (float) num;
	else
	  target->v_double = (double) num;
	break;
      }

      /* We have no distinction between filename and utf8, Lua does
	 not enforce any encoding on the strings. */
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      val->v_string = (gchar *)(optional && lua_isnoneornil (L, narg)
				? NULL : luaL_checkstring (L, narg));
      break;

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	int info_guard = lgi_guard_create_baseinfo (L, info);
	GIInfoType type = g_base_info_get_type (info);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* Directly store underlying value. */
	    marshal_2c_int (L, g_enum_info_get_storage_type (info), val, narg,
			    optional, FALSE);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    {
	      GType gtype = g_registered_type_info_get_g_type (info);
	      nret = lgi_compound_get (L, narg, &gtype, &val->v_pointer,
				       optional);
	      break;
	    }

	  case GI_INFO_TYPE_CALLBACK:
	    nret = marshal_2c_callable (L, info, ai, val, narg, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	lua_remove (L, info_guard);
      }
      break;

    case GI_TYPE_TAG_ARRAY:
      {
	GIArrayType atype = g_type_info_get_array_type (ti);
	switch (atype)
	  {
	  case GI_ARRAY_TYPE_C:
	  case GI_ARRAY_TYPE_ARRAY:
	    nret = marshal_2c_array (L, ti, atype, transfer, val, narg,
				     optional, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	break;
      }

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
      nret = marshal_2c_list (L, ti, tag, transfer, val, narg);
      break;

    default:
      marshal_2c_int (L, tag, val, narg, optional, use_pointer);
    }

  return nret;
}

/* Marshalls single value from GLib/C to Lua.  Returns 1 if something
   was pushed to the stack. */
void
lgi_marshal_2lua (lua_State *L, GITypeInfo *ti, GIArgument *val,
		  GITransfer transfer, gboolean use_pointer,
		  GICallableInfo *ci, void **args)
{
  gboolean own = (transfer != GI_TRANSFER_NOTHING);
  GITypeTag tag = g_type_info_get_tag (ti);
  switch (tag)
    {
    case GI_TYPE_TAG_BOOLEAN:
      lua_pushboolean (L, val->v_boolean);
      break;

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      {
	/* Decide from where to load the number. */
	GIArgument *source;
	if (!use_pointer)
	  /* Marshal directly from val. */
	  source = val;
	else
	  /* Marshal from argument pointed to by value. */
	  source = (GIArgument *) val->v_pointer;

	/* Store read value into chosen source. */
	lua_pushnumber (L, (tag == GI_TYPE_TAG_FLOAT)
			? source->v_float : source->v_double);
	break;
      }

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      lua_pushstring (L, val->v_string);
      if (transfer == GI_TRANSFER_EVERYTHING)
	g_free (val->v_string);
      break;

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	int info_guard = lgi_guard_create_baseinfo (L, info);
	switch (g_base_info_get_type (info))
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* Directly store underlying value. */
	    marshal_2lua_int (L, g_enum_info_get_storage_type (info),
			      val, FALSE);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    lgi_compound_create (L, info, val->v_pointer, own);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	lua_remove (L, info_guard);
      }
      break;

    case GI_TYPE_TAG_ARRAY:
      {
	GIArrayType atype = g_type_info_get_array_type (ti);
	switch (atype)
	  {
	  case GI_ARRAY_TYPE_C:
	  case GI_ARRAY_TYPE_ARRAY:
	    marshal_2lua_array (L, ti, atype, val, transfer, FALSE, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
      }
      break;

    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GLIST:
      marshal_2lua_list (L, ti, val, transfer);
      break;

    default:
      marshal_2lua_int (L, tag, val, use_pointer);
    }
}

void
lgi_marshal_init (lua_State *L)
{}
