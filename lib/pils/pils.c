#include <portability.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

/* Dumbness... */
#define time FooTimeParameter
#define index FooIndexParameter
#	include <glib.h>
#undef time
#undef index


#define ENABLE_PIL_DEFS_PRIVATE
#define ENABLE_PLUGIN_MANAGER_PRIVATE

#include <upmls/MLPlugin.h>
#include "../../libltdl/config.h"

#define NEW(type)		(g_new(type,1))
#define	ZAP(obj)	memset(obj, 0, sizeof(*obj))
#define DELETE(obj)	{g_free(obj); obj = NULL;}

#define PLUGINSUFFIX	LTDL_SHLIB_EXT

static int	PluginDebugLevel = 5;

#define DEBUGPLUGIN	(PluginDebugLevel > 0)



static PIL_rc InterfaceManager_plugin_init(PILPluginUniv* univ);

static char** PILPluginTypeListPlugins(PILPluginType* pitype, int* picount);
static PILInterface* FindIF(PILPluginUniv* universe, const char *iftype
,	const char * ifname);


void	DelPILPluginUniv(PILPluginUniv*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 *
 *	They all follow the same calling sequence though.  It is:
 *		String name"*" type object
 *		"*" type object with the name given by 1st argument
 *		NULL
 *
 *	For example:
 *		RmAPILPluginType takes
 *			string name
 *			PILPluginType* object with the given name.
 */
static gboolean	RmAPILPluginType
(	gpointer pitname	/* Name of this plugin type */
,	gpointer pitype	/* PILPluginType* */
,	gpointer notused
);

static PILPluginType* NewPILPluginType
(	PILPluginUniv* pluginuniv
,	const char *	plugintype
);
static void	DelPILPluginType(PILPluginType*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 */
static gboolean	RmAPILPlugin
(	gpointer piname	/* Name of this plugin  */
,	gpointer plugin		/* PILPlugin* */
,	gpointer notused
);


static PILPlugin* NewPILPlugin(PILPluginType* pitype
	,	const char *	plugin_name
	,	lt_dlhandle	dlhand
	,	PILPluginInitFun PluginSym);
static void	DelPILPlugin(PILPlugin*);

struct MemStat {
	unsigned long	news;
	unsigned long	frees;
};

static struct PluginStats {
	struct MemStat	plugin;
	struct MemStat	pitype;
	struct MemStat	piuniv;
	struct MemStat	interface;
	struct MemStat	interfacetype;
	struct MemStat	interfaceuniv;
}PILstats;

#define	STATNEW(t)	{PILstats.t.news ++; }
#define	STATFREE(t)	{PILstats.t.frees ++; }


static int PILPluginrefcount(PILPluginType*, const char * pluginname);
static int PILPluginpirefcount(PILPluginType* mltype, const char * pluginname
,	int plusminus);

static PILInterfaceUniv*	NewPILInterfaceUniv(PILPluginUniv*);
static void		DelPILInterfaceUniv(PILInterfaceUniv*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 */
static gboolean		RmAPILInterfaceType
(	gpointer iftypename	/* Name of this interface type  */
,	gpointer iftype		/* PILInterfaceType* */
,	gpointer notused
);

static PILInterfaceType*	NewPILInterfaceType
(	PILInterfaceUniv*
,	const char * typename
,	void* ifexports, void* user_data
);
static void		DelPILInterfaceType(PILInterfaceType*);
/*
 *	These RmA* functions are designed to be  called from
 *	hash_table_foreach, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 *	They can be called from other places safely also.
 */
static gboolean		RmAPILInterface
(	gpointer ifname		/* Name of this interface */
,	gpointer plugin		/* PILInterface* */
,	gpointer notused
);

static PILInterface*	NewPILInterface
(	PILInterfaceType*	interfacetype
,	const char*	interfacename
,	void *		exports
,	PILInterfaceFun	closefun
,	void*		ud_interface
);
static void		DelPILInterface(PILInterface*);
static PIL_rc	close_ifmgr_interface(PILInterface*, void*);




/*
 *	For consistency, we show up as a plugin in our our system.
 *
 *	Here are our exports as a plugin.
 *
 */
static const char *	PIL_PILPluginVersion(void);
static void		PIL_PILPluginClose (PILPlugin*);

static const PILPluginOps PluginExports =
{	PIL_PILPluginVersion
,	PILGetDebugLevel		/* also directly exported */
,	PILSetDebugLevel		/* also directly exported */
,	PIL_PILPluginClose
};

/*	Prototypes for the functions that we export to every plugin */
static PIL_rc PILregister_plugin(PILPlugin* piinfo, const PILPluginOps* mops);
static PIL_rc PILunregister_plugin(PILPlugin* piinfo);
static PIL_rc
PILRegisterInterface
(	PILPlugin*	piinfo
,	const char *	interfacetype	/* Type of interface		*/
,	const char *	interfacename	/* Name of interface		*/
,	void*		Ops		/* Ops exported by this interface	*/
,	PILInterfaceFun	closefunc	/* Ops exported by this interface	*/
,	PILInterface**	interfaceid	/* Interface id 	(OP)		*/
,	void**		Imports		/* Functions imported by
					 this interface	(OP)		*/
,	void*		ud_interface	/* interface user data 		*/
);
static PIL_rc	PILunregister_interface(PILInterface* interfaceid);
static void	PILLog(PILLogLevel priority, const char * fmt, ...);


/*
 *	This is the set of functions that we export to every plugin
 *
 *	That also makes it the set of functions that every plugin imports.
 *
 */

static PILPluginImports PILPluginImportSet =
{	PILregister_plugin	/* register_plugin */
,	PILunregister_plugin	/* unregister_plugin */
,	PILRegisterInterface	/* register_interface */
,	PILunregister_interface	/* unregister_interface */
,	PILLoadPlugin		/* load_plugin */
,	PILLog			/* Logging function */
};

static PIL_rc	ifmgr_register_interface(PILInterface* newif
		,		void** imports);
static PIL_rc	ifmgr_unregister_interface(PILInterface* interface);

/*
 *	For consistency, the master interface manager is a interface in the system
 *	Below is our set of exported Interface functions.
 *
 *	Food for thought:  This is the interface manager whose name is interface.
 *	This makes it the Interface Interface interface ;-)
 *		(or the Interface/Interface interface if you prefer)
 */

static PILInterfaceOps  IfExports =
{	ifmgr_register_interface
,	ifmgr_unregister_interface
};



/*
 * Below is the set of functions we export to every interface manager.
 */

static int	IfRefCount(PILInterface * ifh);
static int	IfPluginRefCount(PILInterface*eifinfo,int plusminus);
static void	IfForceUnregister(PILInterface *eifinfo);
static void	IfForEachClientRemove(PILInterface* manangerif
	,	gboolean(*f)(PILInterface* clientif, void * other)
	,	void* other);

static PILInterfaceImports IFManagerImports =
{	IfRefCount
,	IfPluginRefCount
,	IfForceUnregister
,	IfForEachClientRemove
};
static void PILValidatePlugin(gpointer key, gpointer plugin, gpointer pitype);
static void PILValidatePluginType(gpointer key, gpointer pitype, gpointer piuniv);
static void PILValidatePluginUniv(gpointer key, gpointer pitype, gpointer);
static void PILValidateInterface(gpointer key, gpointer interface, gpointer iftype);
static void PILValidateInterfaceType(gpointer key, gpointer iftype, gpointer ifuniv);
static void PILValidateInterfaceUniv(gpointer key, gpointer puniv, gpointer);

/*****************************************************************************
 *
 * This code is for managing plugins, and interacting with them...
 *
 ****************************************************************************/

PILPlugin*
NewPILPlugin(	PILPluginType* pitype
	,	const char *	plugin_name
	,	lt_dlhandle	dlhand
	,	PILPluginInitFun PluginSym)
{
	PILPlugin*	ret = NEW(PILPlugin);

	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILPlugin(0x%x)", (unsigned long)ret);
	}

	STATNEW(plugin);
	ret->MagicNum = PIL_MAGIC_PLUGIN;
	ret->plugin_name = g_strdup(plugin_name);
	ret->plugintype = pitype;
	ret->Interfaces = g_hash_table_new(g_str_hash, g_str_equal);
	ret->refcnt = 0;
	ret->dlhandle = dlhand;
	ret->dlinitfun = PluginSym;
	PILValidatePlugin(ret->plugin_name, ret, pitype);
	return ret;
}
static void
DelPILPlugin(PILPlugin*pi)
{
	DELETE(pi->plugin_name);
	STATFREE(plugin);

	pi->plugintype = NULL;
	g_hash_table_destroy(pi->Interfaces);

	if (pi->refcnt > 0) {
		PILLog(PIL_CRIT, "DelPILPlugin: Non-zero refcnt");
	}

	lt_dlclose(pi->dlhandle);
	ZAP(pi);
	DELETE(pi);
}


static PILPluginType dummymlpitype =
{	PIL_MAGIC_PLUGINTYPE
,	NULL			/*plugintype*/
,	NULL			/*piuniv*/
,	NULL			/*Plugins*/
,	PILPluginrefcount		/* refcount */
,	PILPluginpirefcount	/* pirefcount */
,	PILPluginTypeListPlugins	/* listplugins */
};

static PILPluginType*
NewPILPluginType(PILPluginUniv* pluginuniv
	,	const char *	plugintype
)
{
	PILPluginType*	ret = NEW(PILPluginType);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILPlugintype(0x%x)", (unsigned long)ret);
	}
	STATNEW(pitype);

	*ret = dummymlpitype;

	ret->plugintype = g_strdup(plugintype);
	ret->piuniv = pluginuniv;
	ret->Plugins = g_hash_table_new(g_str_hash, g_str_equal);
	PILValidatePluginType(ret->plugintype, ret, pluginuniv);
	return ret;
}
static void
DelPILPluginType(PILPluginType*pitype)
{
	PILValidatePluginType(NULL, pitype, NULL);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "DelPILPluginType(%s)", pitype->plugintype);
	}

	STATFREE(pitype);
	g_hash_table_foreach_remove(pitype->Plugins, RmAPILPlugin, NULL);
	g_hash_table_destroy(pitype->Plugins);
	DELETE(pitype->plugintype);
	ZAP(pitype);
	DELETE(pitype);
}
/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static gboolean
RmAPILPlugin	/* IsA GHFunc: required for g_hash_table_foreach_remove() */
(	gpointer piname	/* Name of this plugin  */
,	gpointer plugin		/* PILPlugin* */
,	gpointer notused	
)
{
	PILPlugin*	Plugin = plugin;
	PILPluginType*	Pitype = Plugin->plugintype;
	gpointer	key;

	PILValidatePlugin(piname, plugin, NULL);
	PILValidatePluginType(NULL, Pitype, NULL);
	g_assert(IS_PILPLUGIN(Plugin));
	
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "RmAPILPlugin(%s/%s)", Pitype->plugintype
		,	Plugin->plugin_name);
	}
	/* Normally (but not always) called from g_hash_table_forall */

	if (g_hash_table_lookup_extended(Pitype->Plugins
	,	piname, &key, &plugin)) {
		DelPILPlugin(plugin);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
	return TRUE;
}

PILPluginUniv*
NewPILPluginUniv(const char * baseplugindirectory)
{
	PILPluginUniv*	ret = NEW(PILPluginUniv);

	STATNEW(piuniv);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILPluginUniv(0x%x)", (unsigned long)ret);
	}
	if (!g_path_is_absolute(baseplugindirectory)) {
		DELETE(ret);
		return(ret);
	}
	ret->MagicNum = PIL_MAGIC_PLUGINUNIV;
	ret->rootdirectory = g_strdup(baseplugindirectory);

	ret->PluginTypes = g_hash_table_new(g_str_hash, g_str_equal);
	ret->imports = &PILPluginImportSet;
	ret->ifuniv = NewPILInterfaceUniv(ret);
	PILValidatePluginUniv(NULL, ret, NULL);
	return ret;
}

void
DelPILPluginUniv(PILPluginUniv* piuniv)
{


	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "DelPILPluginUniv(0x%lx)"
		,	(unsigned long)piuniv);
	}
	STATFREE(piuniv);
	PILValidatePluginUniv(NULL, piuniv, NULL);
	DelPILInterfaceUniv(piuniv->ifuniv);
	piuniv->ifuniv = NULL;
	g_hash_table_foreach_remove(piuniv->PluginTypes, RmAPILPluginType, NULL);
	g_hash_table_destroy(piuniv->PluginTypes);
	DELETE(piuniv->rootdirectory);
	ZAP(piuniv);
	DELETE(piuniv);
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static gboolean	/* IsA GHFunc: required for g_hash_table_foreach_remove() */
RmAPILPluginType
(	gpointer pitname	/* Name of this plugin type */
,	gpointer pitype	/* PILPluginType* */
,	gpointer notused
)
{
	PILPluginType*	Plugintype = pitype;
	PILPluginUniv*	Pluginuniv = Plugintype->piuniv;
	gpointer	key;

	g_assert(IS_PILPLUGINTYPE(Plugintype));
	PILValidatePluginType(pitname, pitype, NULL);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "RmAPILPluginType(%s)"
		,	Plugintype->plugintype);
	}
	/*
	 * This function is usually but not always called by
	 * g_hash_table_foreach_remove()
	 */

	if (g_hash_table_lookup_extended(Pluginuniv->PluginTypes
	,	pitname, &key, &pitype)) {

		DelPILPluginType(pitype);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
	return TRUE;
}

/*
 *	InterfaceManager_plugin_init: Initialize the handling of
 *	"Interface Manager" interfaces.
 *
 *	There are a few potential bootstraiffng problems here ;-)
 *
 */
static PIL_rc
InterfaceManager_plugin_init(PILPluginUniv* univ)
{
	PILPluginImports* imports = univ->imports;
	PILPluginType*	pitype;
	PILInterface*	ifinfo;
	PILInterfaceType*	iftype;
	void*		dontcare;
	PILPlugin*	ifmgr_plugin;
	PIL_rc		rc;


	iftype = NewPILInterfaceType(univ->ifuniv, PI_IFMANAGER, &IfExports
	,	NULL);

	g_hash_table_insert(univ->ifuniv->iftypes
	,	g_strdup(PI_IFMANAGER), iftype);

	pitype = NewPILPluginType(univ, PI_IFMANAGER);

	g_hash_table_insert(univ->PluginTypes
	,	g_strdup(PI_IFMANAGER), pitype);

	ifmgr_plugin= NewPILPlugin(pitype, PI_IFMANAGER, NULL, NULL);

	g_hash_table_insert(pitype->Plugins
	,	g_strdup(PI_IFMANAGER), ifmgr_plugin);

	/* We can call register_plugin, since it doesn't depend on us... */
	rc = imports->register_plugin(ifmgr_plugin, &PluginExports);
	if (rc != PIL_OK) {
		PILLog(PIL_CRIT, "register_plugin() failed in init: %s"
		,	PIL_strerror(rc));
		return(rc);
	}
	/*
	 * Now, we're registering interfaces, and are into some deep
	 * Catch-22 if do it the "easy" way, since our code is
	 * needed in order to support interface loading for the type of
	 * interface we are (a Interface interface).
	 *
	 * So, instead of calling imports->register_interface(), we have to
	 * do the work ourselves here...
	 *  
	 * Since no one should yet be registered to handle Interface
	 * interfaces, we need to bypass the hash table handler lookup
	 * that register_interface would do and call the function that
	 * register_interface would call...
	 *
	 */

	/* The first argument is the PILInterfaceType* */
	ifinfo = NewPILInterface(iftype, PI_IFMANAGER, &IfExports
	,	close_ifmgr_interface, NULL);
	ifinfo->ifmanager = iftype->ifmgr_ref = ifinfo;
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "InterfaceManager_plugin_init(0x%lx/%s)"
		,	(unsigned long)ifinfo, ifinfo->interfacename);
	}
	PILValidatePluginUniv(NULL, univ, NULL);
	ifmgr_register_interface(ifinfo, &dontcare);
	PILValidatePluginUniv(NULL, univ, NULL);

	return(PIL_OK);
}/*InterfaceManager_plugin_init*/


/* Return current IfIf "plugin" version (not very interesting for us) */
static const char *
PIL_PILPluginVersion(void)
{
	return("1.0");
}

/* Return current IfIf debug level */
int
PILGetDebugLevel(void)
{
	return(PluginDebugLevel);
}

/* Set current IfIf debug level */
void
PILSetDebugLevel (int level)
{
	PluginDebugLevel = level;
}

/* Close/shutdown our PILPlugin (the interface manager interface plugin) */
/* All our interfaces will have already been shut down and unregistered */
static void
PIL_PILPluginClose (PILPlugin* plugin)
{
}

/*****************************************************************************
 *
 * This code is for managing interfaces, and interacting with them...
 *
 ****************************************************************************/


static PILInterface*
NewPILInterface(PILInterfaceType*	interfacetype
	,	const char*	interfacename
	,	void *		exports
	,	PILInterfaceFun	closefun
	,	void*		ud_interface)
{
	PILInterface*	ret = NULL;
	PILInterface*	look = NULL;


	if ((look = g_hash_table_lookup(interfacetype->interfaces, interfacename))
	!=		NULL) {
		DelPILInterface(look);
	}
	ret = NEW(PILInterface);
	STATNEW(interface);
	ret->MagicNum = PIL_MAGIC_INTERFACE;
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILInterface(0x%x)", (unsigned long)ret);
	}

	if (ret) {
		ret->interfacetype = interfacetype;
		ret->exports = exports;
		ret->ud_interface = ud_interface;
		ret->interfacename = g_strdup(interfacename);
		ret->ifmanager = interfacetype->ifmgr_ref;
		g_hash_table_insert(interfacetype->interfaces
		,	g_strdup(ret->interfacename), ret);
		ret->if_close = closefun;
		ret->refcnt = 1;
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG, "NewPILInterface(0x%lx:%s/%s)**********"
			,	(unsigned long)ret
			,	interfacetype->typename
			,	ret->interfacename);
		}
	}
	return ret;
}
static void
DelPILInterface(PILInterface* intf)
{
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "DelPILInterface(0x%lx/%s)"
		,	(unsigned long)intf, intf->interfacename);
	}
	STATFREE(interface);
	DELETE(intf->interfacename);
	ZAP(intf);
	DELETE(intf);
}

static PILInterfaceType*
NewPILInterfaceType(PILInterfaceUniv*univ, const char * typename
,	void* ifeports, void* user_data)
{
	PILInterfaceType*	ifmgr_types;
	PILInterface*	ifmgr_ref;
	PILInterfaceType*	ret = NEW(PILInterfaceType);


	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILInterfaceType(0x%x)", (unsigned long)ret);
	}
	STATNEW(interfacetype);
	ret->MagicNum = PIL_MAGIC_INTERFACETYPE;
	ret->typename = g_strdup(typename);
	ret->interfaces = g_hash_table_new(g_str_hash, g_str_equal);
	ret->ud_if_type = user_data;
	ret->universe = univ;
	ret->ifmgr_ref = NULL;
	/* Now find the pointer to our interface type in the Interface Universe*/
	if ((ifmgr_types = g_hash_table_lookup(univ->iftypes, PI_IFMANAGER))
	!= NULL) {
		if ((ifmgr_ref=g_hash_table_lookup(ifmgr_types->interfaces
		,	typename)) != NULL) {
			ret->ifmgr_ref = ifmgr_ref;
		}else {
		      g_assert(strcmp(typename, PI_IFMANAGER) == 0);
		}
	}else {
		g_assert(strcmp(typename, PI_IFMANAGER) == 0);
	}
	return ret;
}
static void
DelPILInterfaceType(PILInterfaceType*ift)
{
	PILInterfaceUniv*	u = ift->universe;
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "DelPILInterfaceType(%s)"
		,	ift->typename);
	}
	STATFREE(interfacetype);

	PILValidateInterfaceUniv(NULL, u, NULL);

	/*
	 *	RmAPILInterface refuses to remove the interface for the Interface
	 *	manager, because it must be removed last.
	 *
	 *	Otherwise we won't be able to unregister interfaces
	 *	for other types of objects, and we'll be very confused.
	 */

	g_hash_table_foreach_remove(ift->interfaces, RmAPILInterface, NULL);

	PILValidateInterfaceUniv(NULL, u, NULL);

	if (g_hash_table_size(ift->interfaces) > 0) {
		gpointer	key, iftype;
		PILLog(PIL_DEBUG, "DelPILInterfaceType(%s): table size (%d)"
		,	ift->typename, g_hash_table_size(ift->interfaces));
		if (g_hash_table_lookup_extended(ift->interfaces
		,	PI_IFMANAGER, &key, &iftype)) {
			g_hash_table_remove(ift->interfaces, key);
			DelPILInterface((PILInterface*)iftype);
			DELETE(key);
		}
	}
	DELETE(ift->typename);
	g_hash_table_destroy(ift->interfaces);
	ZAP(ift);
	DELETE(ift);
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static gboolean	/* IsAGHFunc: required for g_hash_table_foreach_remove() */
RmAPILInterface
(	gpointer ifname	/* Name of this interface */
,	gpointer intf	/* PILInterface* */
,	gpointer notused
)
{
	PILInterface*	If = intf;
	PILInterfaceType*	Iftype = If->interfacetype;
	gpointer	key;

	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "RmAPILInterface(0x%lx/%s)"
		,	(unsigned long)If, If->interfacename);
	}
	g_assert(IS_PILINTERFACE(If));

	/*
	 * Don't remove the master interface manager this way, or
	 * Somebody will have a cow... 
	 */
	if (If == If->ifmanager) {
		return FALSE;
	}
	PILValidateInterface(ifname, If, Iftype);
	PILValidateInterfaceType(NULL, Iftype, NULL);

	/*
	 * This function is usually but not always called by
	 * g_hash_table_foreach_remove()
	 */

	if (g_hash_table_lookup_extended(Iftype->interfaces
	,	ifname, &key, &intf)) {
		g_assert(intf == If);
		g_assert(strcmp(key, (char*)ifname) == 0);
		PILunregister_interface(If);
		DELETE(key);
		DelPILInterface(If);
	}else{
		g_assert_not_reached();
	}
	return TRUE;
}


/* Register a Interface Interface (Interface manager) */
static PIL_rc
ifmgr_register_interface(PILInterface* intf
,		void**	imports)
{
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG
		, 	"Registering Interface manager for type '%s'"
		,	intf->interfacename);
	}
	*imports = &IFManagerImports;
	return PIL_OK;
}

static gboolean
RemoveAllClients(PILInterface*interface, void * managerif)
{
	/*
	 * Careful!  We can't remove ourselves this way... 
	 * This gets taken care of as a special case in DelPILInterfaceUniv...
	 */
	if (managerif == interface) {
		return FALSE;
	}
	PILunregister_interface(interface);
	return TRUE;
}

/* Unconditionally unregister a interface manager (Interface Interface) */
static PIL_rc
ifmgr_unregister_interface(PILInterface* interface)
{
	/*
	 * We need to unregister every interface we manage
	 */
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "ifmgr_unregister_interface(%s)"
		,	interface->interfacename);
	}

	IfForEachClientRemove(interface, RemoveAllClients, interface);
	return PIL_OK;
}

/*	Called to close the Interface manager for type Interface */
static PIL_rc
close_ifmgr_interface(PILInterface* us, void* ud_interface)
{
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "close_ifmgr_interface(%s)"
		,	us->interfacename);
	}
	/* Nothing much to do */
	return PIL_OK;
}

/* Return the reference count for this interface */
static int
IfRefCount(PILInterface * eifinfo)
{
	return eifinfo->refcnt;
}
 
/* Return the reference count for this interface */
static int
IfPluginRefCount(PILInterface*eifinfo, int plusminus)
{
	eifinfo->refcnt += plusminus;
	if (eifinfo->refcnt <= 0) {
		/* Unregister this interface? FIXME!! */
		eifinfo = 0;
	}
	return eifinfo->refcnt;
}

static PILInterface*
FindIF(PILPluginUniv* universe, const char *iftype, const char * ifname)
{
	PILInterfaceUniv*	puniv;
	PILInterfaceType*	ptype;

	if (universe == NULL || (puniv = universe->ifuniv) == NULL
	||	(ptype=g_hash_table_lookup(puniv->iftypes, iftype))==NULL){
		return NULL;
	}
	return g_hash_table_lookup(ptype->interfaces, ifname);
}

PIL_rc		
PILIncrIFRefCount(PILPluginUniv* mu
,		const char *	interfacetype
,		const char *	interfacename
,		int	plusminus)
{
		PILInterface*	intf = FindIF(mu, interfacetype, interfacename);

		if (intf) {
			IfPluginRefCount(intf, plusminus);
			return PIL_OK;
		}
		return PIL_NOPLUGIN;
}

int
PILGetIFRefCount(PILPluginUniv*	mu
,		const char *	interfacetype
,		const char *	interfacename)
{
		PILInterface*	intf = FindIF(mu, interfacetype, interfacename);

		if (!intf) {
			return -1;
		}
		return intf->refcnt;
}

static void
IfForceUnregister(PILInterface *id)
{
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "IfForceUnRegister(%s)"
		,	id->interfacename);
	}
	RmAPILInterface(id->interfacename, id, NULL);
}

struct f_e_c_helper {
	gboolean(*fun)(PILInterface* clientif, void * passalong);
	void*	passalong;
};

static gboolean IfForEachClientHelper(gpointer key
,	gpointer iftype, gpointer helper_v);

static gboolean
IfForEachClientHelper(gpointer unused, gpointer iftype, gpointer v)
{
	struct f_e_c_helper*	s = (struct f_e_c_helper*)v;

	g_assert(IS_PILINTERFACE((PILInterface*)iftype));
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "IfForEachClientHelper(%s)"
		,	((PILInterface*)iftype)->interfacename);
	}

	return s->fun((PILInterface*)iftype, s->passalong);
}


static void
IfForEachClientRemove
(	PILInterface* mgrif
,	gboolean(*f)(PILInterface* clientif, void * passalong)
,	void* passalong
)
{
	PILInterfaceType*	mgrt;
	PILInterfaceUniv*	u;
	const char *	ifname;
	PILInterfaceType*	clientt;

	struct f_e_c_helper	h = {f, passalong};
		

	if (mgrif == NULL || (mgrt = mgrif->interfacetype) == NULL
	||	(u = mgrt->universe) == NULL
	||	(ifname = mgrif->interfacename) == NULL) {
		PILLog(PIL_WARN, "bad parameters to IfForEachClientRemove");
		return;
	}

	if ((clientt = g_hash_table_lookup(u->iftypes, ifname)) == NULL) {
		PILLog(PIL_WARN, "Interface manager [%s/%s] has no clients"
		,	PI_IFMANAGER, ifname);
		return;
	};
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "IfForEachClientRemove(%s:%s)"
		,	mgrt->typename, clientt->typename);
	}
	if (clientt->ifmgr_ref != mgrif) {
		PILLog(PIL_WARN, "Bad ifmgr_ref ptr in PILInterfaceType");
		return;
	}

	g_hash_table_foreach_remove(clientt->interfaces, IfForEachClientHelper, &h);
}

static PIL_rc
PILregister_plugin(PILPlugin* piinfo, const PILPluginOps* commonops)
{
	piinfo->pluginops = commonops;
	return PIL_OK;
}

static PIL_rc
PILunregister_plugin(PILPlugin* piinfo)
{
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "PILunregister_plugin(%s)"
		,	piinfo->plugin_name);
	}
	RmAPILPlugin(piinfo->plugin_name, piinfo, NULL);
	return PIL_OK;
}

/* General logging function (not really UPPILS-specific) */
static void
PILLog(PILLogLevel priority, const char * format, ...)
{
	va_list		args;
	GLogLevelFlags	flags;

	switch(priority) {
		case PIL_FATAL:	flags = G_LOG_LEVEL_ERROR;
			break;
		case PIL_CRIT:	flags = G_LOG_LEVEL_CRITICAL;
			break;

		default:	/* FALL THROUGH... */
		case PIL_WARN:	flags = G_LOG_LEVEL_WARNING;
			break;

		case PIL_INFO:	flags = G_LOG_LEVEL_INFO;
			break;
		case PIL_DEBUG:	flags = G_LOG_LEVEL_DEBUG;
			break;
	};
	va_start (args, format);
	g_logv (G_LOG_DOMAIN, flags, format, args);
	va_end (args);
}

static const char * PIL_strerrmsgs [] =
{	"Success"
,	"Invalid Parameters"
,	"Bad plugin/interface type"
,	"Duplicate entry (plugin/interface name/type)"
,	"Oops happens"
,	"No such plugin/interface/interface type"
};

const char *
PIL_strerror(PIL_rc rc)
{
	int	irc = (int) rc;
	static	char buf[128];

	if (irc < 0 || irc >= DIMOF(PIL_strerrmsgs)) {
		snprintf(buf, sizeof(buf), "return code %d (?)", irc);
		return buf;
	}
	return PIL_strerrmsgs[irc];
}


/*
 * PILLoadPlugin()	- loads a plugin into memory and calls the
 * 			initial() entry point in the plugin.
 *
 *
 * Method:
 *
 * 	Construct file name of plugin.
 * 	See if plugin exists.  If not, fail with PIL_NOPLUGIN.
 *
 *	Search Universe for plugin type
 *		If found, search plugin type for pluginname
 *			if found, fail with PIL_EXIST.
 *		Otherwise,
 *			Create new Plugin type structure
 *	Use lt_dlopen() on plugin to get lt_dlhandle for it.
 *
 *	Construct the symbol name of the initialization function.
 *
 *	Use lt_dlsym() to find the pointer to the init function.
 *
 *	Call the initialization function.
 */
PIL_rc
PILLoadPlugin(PILPluginUniv* universe, const char * plugintype
,	const char *	pluginname
,	void*		plugin_user_data)
{
	char * PluginPath;
	char * PluginSym;
	PILPluginType*	pitype;
	PILPlugin*	piinfo;
	lt_dlhandle	dlhand;
	PILPluginInitFun	initfun;

	PluginPath = g_strdup_printf("%s%s%s%s%s%s"
	,	universe->rootdirectory
	,	G_DIR_SEPARATOR_S
	,	plugintype
	,	G_DIR_SEPARATOR_S
	,	pluginname
	,	LTDL_SHLIB_EXT);

	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "Plugin path for %s/%s => [%s]"
		,	plugintype, pluginname, PluginPath);
	}

	/* Make sure we can read and execute the plugin file */
	/* This test is nice, because dlopen reasons aren't return codes */

	if (access(PluginPath, R_OK|X_OK) != 0) {
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG, "Plugin file %s does not exist"
			,	PluginPath);
		}
		DELETE(PluginPath);
		return PIL_NOPLUGIN;
	}

	if((pitype=g_hash_table_lookup(universe->PluginTypes, plugintype))
	!= NULL) {
		if ((piinfo = g_hash_table_lookup
		(	pitype->Plugins, pluginname)) != NULL) {

			if (DEBUGPLUGIN) {
				PILLog(PIL_DEBUG, "Plugin %s already loaded"
				,	PluginPath);
			}
			DELETE(PluginPath);
			return PIL_EXIST;
		}
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG, "PluginType %s already present"
			,	plugintype);
		}
	}else{
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG, "Creating PluginType for %s"
			,	plugintype);
		}
		/* Create a new PILPluginType object */
		pitype = NewPILPluginType(universe, plugintype);
	}

	g_assert(pitype != NULL);

	/*
	 * At this point, we have a PILPluginType object and our
	 * plugin name is not listed in it.
	 */

	dlhand = lt_dlopen(PluginPath);
	DELETE(PluginPath);

	if (!dlhand) {
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG
			,	"lt_dlopen() failure on plugin %s/%s."
			" Reason: [%s]"
			,	plugintype, pluginname
			,	lt_dlerror());
		}
		DelPILPluginType(pitype);
		return PIL_NOPLUGIN;
	}
	/* Construct the magic init function symbol name */
	PluginSym = g_strdup_printf(PIL_FUNC_FMT
	,	plugintype, pluginname);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "Plugin %s/%s  init function: %s"
		,	plugintype, pluginname
		,	PluginSym);
	}

	initfun = lt_dlsym(dlhand, PluginSym);
	DELETE(PluginSym);

	if (initfun == NULL) {
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG
			,	"Plugin %s/%s init function not found"
			,	plugintype, pluginname);
		}
		lt_dlclose(dlhand); dlhand=NULL;
		DelPILPluginType(pitype);
		return PIL_NOPLUGIN;
	}
	/*
	 *	Construct the new PILPlugin object
	 */
	piinfo = NewPILPlugin(pitype, pluginname, dlhand, initfun);
	g_assert(piinfo != NULL);
	g_hash_table_insert(pitype->Plugins, g_strdup(piinfo->plugin_name), piinfo);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "Plugin %s/%s loaded and constructed."
		,	plugintype, pluginname);
	}
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "Calling init function in plugin %s/%s."
		,	plugintype, pluginname);
	}
	/* Save away the user_data for later */
	piinfo->ud_plugin = plugin_user_data;
	initfun(piinfo, universe->imports, plugin_user_data);

	return PIL_OK;
}/*PILLoadPlugin*/

#define REPORTERR(msg)	PILLog(PIL_CRIT, "%s", msg)

/*
 *	Register a interface.
 *
 *	This function is exported to plugins for their use.
 */
static PIL_rc
PILRegisterInterface(PILPlugin* piinfo
,	const char *	interfacetype	/* Type of interface	*/
,	const char *	interfacename	/* Name of interface	*/
,	void*		Ops		/* Info (functions) exported
					   by this interface	*/
,	PILInterfaceFun	close_func	/* Close function for interface */
,	PILInterface**	interfaceid	/* Interface id 	(OP)	*/
,	void**		Imports		/* Functions imported by
					 this interface	(OP)	*/
,	void*		ud_interface	/* Optional user_data */
)
{
	PILPluginUniv*	piuniv;	/* Universe this plugin is in */
	PILPluginType*	pitype;	/* Type of this plugin */
	PILInterfaceUniv*	ifuniv;		/* Universe this interface is in */
	PILInterfaceType*iftype;		/* Type of this interface */
	PILInterface*	ifinfo;		/* Info about this Interface */

	PILInterfaceType*ifmgrtype;	/* PILInterfaceType for PI_IFMANAGER */
	PILInterface*	ifmgrinfo;	/* Interface info for "interfacetype" */
	const PILInterfaceOps* ifops;	/* Ops vector for InterfaceManager */
					/* of type "interfacetype" */
	PIL_rc		rc;

	if (	 piinfo == NULL
	||	(pitype = piinfo->plugintype)	== NULL
	||	(piuniv = pitype->piuniv)	== NULL
	||	(ifuniv = piuniv->ifuniv)	== NULL
	||	ifuniv->iftypes			== NULL
	||	close_func			== NULL
	) {
		REPORTERR("bad parameters to PILRegisterInterface");
		return PIL_INVAL;
	}

	/* Now we have lots of info, but not quite enough... */

	if ((iftype = g_hash_table_lookup(ifuniv->iftypes, interfacetype))
	==	NULL) {

		/* Try to autoload the needed interface handler */
		(void)PILLoadPlugin(piuniv, PI_IFMANAGER, interfacetype, NULL);

		/* See if the interface handler loaded like we expect */
		if ((iftype = g_hash_table_lookup(ifuniv->iftypes
		,	interfacetype)) ==	NULL) {
			return PIL_BADTYPE;
		}
	}
	if ((ifinfo = g_hash_table_lookup(iftype->interfaces, interfacename))
	!=	NULL) {
		g_warning("Attempt to register duplicate interface: %s/%s"
		,	interfacetype, interfacename);
		return PIL_EXIST;
	}
	/*
	 * OK...  Now we know it is valid, and isn't registered...
	 * Let's locate the InterfaceManager registrar for this type
	 */
	if ((ifmgrtype = g_hash_table_lookup(ifuniv->iftypes, PI_IFMANAGER))
	==	NULL) {
		REPORTERR("No " PI_IFMANAGER " type!");
		return PIL_OOPS;
	}
	if ((ifmgrinfo = g_hash_table_lookup(ifmgrtype->interfaces, interfacetype))
	==	NULL) {
		PILLog(PIL_CRIT
		,	"No interface manager for given type (%s) !"
		,	interfacetype);
		return PIL_BADTYPE;
	}

	ifops = ifmgrinfo->exports;

	/* Now we have all the information anyone could possibly want ;-) */

	ifinfo = NewPILInterface(iftype, interfacename, Ops, close_func, ud_interface);
	*interfaceid = ifinfo;

	/* Call the registration function for our interface type */
	rc = ifops->RegisterInterface(ifinfo, Imports);

	/* Increment reference count of interface manager */
	IfPluginRefCount(ifmgrinfo, 1);
	return rc;
}

/*
 * Method:
 *
 *	Verify interface is valid.
 *
 *	Call interface close function.
 *
 *	Call interface manager unregister function
 *
 *	Call RmAPILInterface to remove from InterfaceType table, and
 *		free interface object.
 *
 */

static PIL_rc
PILunregister_interface(PILInterface* id)
{
	PILInterfaceType*	t;
	PILInterfaceUniv*	u;
	PIL_rc		rc;
	PILInterface*	ifmgr_info;	/* Pointer to our interface handler */
	const PILInterfaceOps* exports;	/* InterfaceManager operations  for the
					 * type of interface we are
					 */

	if (	 id == NULL
	||	(t = id->interfacetype) == NULL
	||	(u = t->universe) == NULL
	|| 	id->interfacename == NULL
	||	id->if_close == NULL) {
		PILLog(PIL_WARN, "PILunregister_interface: bad interfaceid");
		return PIL_INVAL;
	}
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "PILunregister_interface(%s/%s)"
		,	t->typename, id->interfacename);
	}

	/* Call the close function supplied by the interface */

	if ((rc=id->if_close(id, id->ud_interface)) != PIL_OK) {
		PILLog(PIL_WARN, "InterfaceClose on %s/%s returned %s"
		,	t->typename, id->interfacename
		,	PIL_strerror(rc));
	}

	/* Find the InterfaceManager that manages us */
	ifmgr_info = t->ifmgr_ref;

	g_assert(ifmgr_info != NULL);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "PILunregister_interface(%s/%s)#2"
		,	ifmgr_info->interfacename, id->interfacename);
	}

	/* Find the exported functions from that IFIF */
	exports =  ifmgr_info->exports;

	g_assert(exports != NULL && exports->UnRegisterInterface != NULL);

	/* Call the interface manager unregister function */
	exports->UnRegisterInterface(id);

	/* Decrement reference count of interface manager */
	IfPluginRefCount(ifmgr_info, -1);

	/* FIXME!! We need to delete this outside this function... */
#if 0
	RmAPILInterface(id->interfacename, id, NULL);
#endif

	return rc;
}

static PILInterfaceUniv*
NewPILInterfaceUniv(PILPluginUniv* piuniv)
{
	PILInterfaceUniv*	ret = NEW(PILInterfaceUniv);

	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "NewPILInterfaceUniv(0x%x)"
		,	(unsigned long)ret);
	}
	STATNEW(interfaceuniv);
	ret->MagicNum = PIL_MAGIC_INTERFACEUNIV;
	/* Make the two universes point at each other */
	ret->piuniv = piuniv;
	piuniv->ifuniv = ret;

	ret->iftypes = g_hash_table_new(g_str_hash, g_str_equal);

	InterfaceManager_plugin_init(piuniv);
	return ret;
}

static void
DelPILInterfaceUniv(PILInterfaceUniv* ifuniv)
{
	g_assert(ifuniv!= NULL && ifuniv->iftypes != NULL);
	PILValidateInterfaceUniv(NULL, ifuniv, NULL);

	STATFREE(interfaceuniv);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "DelPILInterfaceUniv(0x%lx)"
		,	(unsigned long) ifuniv);
	}
	g_hash_table_foreach_remove(ifuniv->iftypes, RmAPILInterfaceType, NULL);
	g_hash_table_destroy(ifuniv->iftypes);
	ZAP(ifuniv);
	DELETE(ifuniv);
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static gboolean	/* IsA GHFunc: required for g_hash_table_foreach_remove() */
RmAPILInterfaceType
(	gpointer typename	/* Name of this interface type  */
,	gpointer iftype		/* PILInterfaceType* */
,	gpointer notused
)
{
	gpointer	key;
	PILInterfaceType*	Iftype = iftype;
	PILInterfaceUniv*	Ifuniv = Iftype->universe;

	/*
	 * We are not always called by g_hash_table_foreach_remove()
	 */

	g_assert(IS_PILINTERFACETYPE(Iftype));
	PILValidateInterfaceUniv(NULL, Ifuniv, NULL);
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "RmAPILInterfaceType(%s)"
		,	(char*)typename);
	}

	if (g_hash_table_lookup_extended(Ifuniv->iftypes
	,	typename, &key, &iftype)) {

		PILValidateInterfaceUniv(NULL, Ifuniv, NULL);
		g_assert(iftype == Iftype);
		g_assert(strcmp(key, (char*)typename) == 0);
		DelPILInterfaceType(iftype);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
	return TRUE;
}

static int
PILPluginrefcount(PILPluginType* pitype, const char * pluginname)
{
	PILPlugin*	piinfo;

	if ((piinfo = g_hash_table_lookup(pitype->Plugins, pluginname))
	==	NULL) {
		return -1;
	}
	return piinfo->refcnt;
}
static int
PILPluginpirefcount(PILPluginType* pitype, const char * pluginname
,	int plusminus)
{
	PILPlugin*	piinfo;

	if ((piinfo = g_hash_table_lookup(pitype->Plugins, pluginname))
	==	NULL) {
		return -1;
	}
	if ((piinfo->refcnt += plusminus) < 0) {
		piinfo->refcnt = 0;
	}
	return piinfo->refcnt;
}


/*
 * We need to write more functions:  These include...
 *
 * Plugin functions:
 *
 * PILPluginPath()	- returns path name for a given plugin
 *
 * PILPluginTypeList()	- returns list of plugins of a given type
 *
 */
static void free_dirlist(struct dirent** dlist, int n);

static int qsort_string_cmp(const void *a, const void *b);


static void
free_dirlist(struct dirent** dlist, int n)
{
	int	j;
	for (j=0; j < n; ++j) {
		if (dlist[j]) {
			free(dlist[j]);
			dlist[j] = NULL;
		}
	}
	free(dlist);
}

static int
qsort_string_cmp(const void *a, const void *b)
{
	return(strcmp(*(const char * const *)a, *(const char * const *)b));
}

#define FREE_DIRLIST(dlist, n)	{free_dirlist(dlist, n); dlist = NULL;}

static int
so_select (const struct dirent *dire)
{ 
    
	const char obj_end [] = PLUGINSUFFIX;
	const char *end = &dire->d_name[strlen(dire->d_name)
	-	(STRLEN(obj_end))];
	
	
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG, "In so_select: %s.", dire->d_name);
	}
	if (obj_end < dire->d_name) {
			return 0;
	}
	if (strcmp(end, obj_end) == 0) {
		if (DEBUGPLUGIN) {
			PILLog(PIL_DEBUG, "FILE %s looks like a plugin name."
			,	dire->d_name);
		}
		return 1;
	}
	if (DEBUGPLUGIN) {
		PILLog(PIL_DEBUG
		,	"FILE %s Doesn't look like a plugin name [%s] "
		"%d %d %s."
		,	dire->d_name, end
		,	sizeof(obj_end), strlen(dire->d_name)
		,	&dire->d_name[strlen(dire->d_name)
		-	(sizeof(obj_end)-1)]);
	}
	
	return 0;
}

/* Return (sorted) list of available plugin names */
static char**
PILPluginTypeListPlugins(PILPluginType* pitype
,	int *		picount	/* Can be NULL ... */)
{
	const char *	basedir = pitype->piuniv->rootdirectory;
	const char *	piclass = pitype->plugintype;
	GString*	path;
	char **		result = NULL;
	struct dirent**	files;
	int		plugincount;
	int		j;


	path = g_string_new(basedir);
	if (piclass) {
		if (g_string_append_c(path, G_DIR_SEPARATOR) == NULL
		||	g_string_append(path, piclass) == NULL) {
			g_string_free(path, 1); path = NULL;
			return(NULL);
		}
	}

	plugincount = scandir(path->str, &files
	,	SCANSEL_CAST &so_select, NULL);
	g_string_free(path, 1); path=NULL;

	result = (char **) g_malloc((plugincount+1)*sizeof(char *));

	for (j=0; j < plugincount; ++j) {
		char*	s;
		int	slen = strlen(files[j]->d_name)
		-	STRLEN(PLUGINSUFFIX);

		s = g_malloc(slen+1);
		strncpy(s, files[j]->d_name, slen);
		s[slen] = EOS;
		result[j] = s;
	}
	result[j] = NULL;
	FREE_DIRLIST(files, plugincount);

	/* Return them in sorted order... */
	qsort(result, plugincount, sizeof(char *), qsort_string_cmp);

	if (picount != NULL) {
		*picount = plugincount;
	}

	return(result);
}

void
PILFreePluginList(char ** pluginlist)
{
	char **	ml = pluginlist;

	while (*ml != NULL) {
		DELETE(*ml);
	}
	DELETE(pluginlist);
}


static void
PILValidatePlugin(gpointer key, gpointer plugin, gpointer pitype)
{
	const char * Key = key;
	const PILPlugin * Plugin = plugin;

	g_assert(IS_PILPLUGIN(Plugin));

	g_assert(Key == NULL || strcmp(Key, Plugin->plugin_name) == 0);
	g_assert (Plugin->Interfaces != NULL);

	g_assert (Plugin->refcnt >= 0 );

	/* g_assert (Plugin->pluginops != NULL ); */
	g_assert (strcmp(Key, PI_IFMANAGER) == 0 || Plugin->dlinitfun != NULL );
	g_assert (strcmp(Plugin->plugin_name, PI_IFMANAGER) == 0
	||	Plugin->dlhandle != NULL);
	g_assert(Plugin->plugintype != NULL);
	g_assert(IS_PILPLUGINTYPE(Plugin->plugintype));
	g_assert(pitype == NULL || pitype == Plugin->plugintype);
}

static void
PILValidatePluginType(gpointer key, gpointer pitype, gpointer piuniv)
{
	char * Key = key;
	PILPluginType * Pitype = pitype;
	PILPluginUniv * Muniv = piuniv;

	g_assert(IS_PILPLUGINTYPE(Pitype));
	g_assert(Muniv == NULL || IS_PILPLUGINUNIV(Muniv));
	g_assert(Key == NULL || strcmp(Key, Pitype->plugintype) == 0);
	g_assert(IS_PILPLUGINUNIV(Pitype->piuniv));
	g_assert(piuniv == NULL || piuniv == Pitype->piuniv);
	g_assert(Pitype->Plugins != NULL);
	g_hash_table_foreach(Pitype->Plugins, PILValidatePlugin, Pitype);
}
static void PILValidatePluginUniv(gpointer key, gpointer piuniv, gpointer dummy)
{
	PILPluginUniv * Muniv = piuniv;

	g_assert(IS_PILPLUGINUNIV(Muniv));
	g_assert(Muniv->rootdirectory != NULL);
	g_assert(Muniv->imports != NULL);
	g_hash_table_foreach(Muniv->PluginTypes, PILValidatePluginType, piuniv);
	PILValidateInterfaceUniv(NULL, Muniv->ifuniv, piuniv);
}
static void
PILValidateInterface(gpointer key, gpointer interface, gpointer iftype)
{
	char *		Key = key;
	PILInterface*	Interface = interface;
	g_assert(IS_PILINTERFACE(Interface));
	g_assert(Key == NULL || strcmp(Key, Interface->interfacename) == 0);
	g_assert(IS_PILINTERFACETYPE(Interface->interfacetype));
	g_assert(iftype == NULL || iftype == Interface->interfacetype);
	g_assert(Interface->ifmanager!= NULL);
	g_assert(IS_PILINTERFACE(Interface->ifmanager));
	g_assert(strcmp(Interface->interfacetype->typename
	,	Interface->ifmanager->interfacename)== 0);
	g_assert(Interface->exports != NULL);
	g_assert(Interface->if_close != NULL);
}
static void
PILValidateInterfaceType(gpointer key, gpointer iftype, gpointer ifuniv)
{
	char *		Key = key;
	PILInterfaceType*	Iftype = iftype;
	g_assert(IS_PILINTERFACETYPE(Iftype));
	g_assert(Key == NULL || strcmp(Key, Iftype->typename) == 0);
	g_assert(ifuniv == NULL || Iftype->universe == ifuniv);
	g_assert(Iftype->interfaces != NULL);
	g_assert(Iftype->ifmgr_ref != NULL);
	g_assert(IS_PILINTERFACE(Iftype->ifmgr_ref));
	g_assert(Key == NULL || strcmp(Key, Iftype->ifmgr_ref->interfacename) == 0);

	g_hash_table_foreach(Iftype->interfaces, PILValidateInterface, iftype);
}
static void
PILValidateInterfaceUniv(gpointer key, gpointer ifuniv, gpointer piuniv)
{
	PILInterfaceUniv*	Ifuniv = ifuniv;
	PILPluginUniv*	Pluginuniv = piuniv;
	g_assert(IS_PILINTERFACEUNIV(Ifuniv));
	g_assert(Pluginuniv == NULL || IS_PILPLUGINUNIV(Pluginuniv));
	g_assert(piuniv == NULL || piuniv == Ifuniv->piuniv);
	g_hash_table_foreach(Ifuniv->iftypes, PILValidateInterfaceType, ifuniv);
}

#define PRSTAT(type)	{					\
	PILLog(PIL_INFO, "Plugin system objects (" #type "): "		\
	"\tnew %ld free \%ld current %ld"			\
	,	PILstats.type.news				\
	,	PILstats.type.frees				\
	,	PILstats.type.news - PILstats.type.frees);	\
}
void
PILLogMemStats(void)
{
	PRSTAT(plugin);
	PRSTAT(pitype);
	PRSTAT(piuniv);
	PRSTAT(interface);
	PRSTAT(interfacetype);
	PRSTAT(interfaceuniv);
}
