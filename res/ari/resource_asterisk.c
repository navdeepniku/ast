/* -*- C -*-
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Implementation for ARI stubs.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/ast_version.h"
#include "asterisk/buildinfo.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "resource_asterisk.h"

void ast_ari_asterisk_get_info(struct ast_variable *headers,
	struct ast_ari_asterisk_get_info_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	int show_all = args->only_count == 0;
	int show_build = show_all;
	int show_system = show_all;
	int show_config = show_all;
	int show_status = show_all;
	size_t i;
	int res = 0;

	for (i = 0; i < args->only_count; ++i) {
		if (strcasecmp("build", args->only[i]) == 0) {
			show_build = 1;
		} else if (strcasecmp("system", args->only[i]) == 0) {
			show_system = 1;
		} else if (strcasecmp("config", args->only[i]) == 0) {
			show_config = 1;
		} else if (strcasecmp("status", args->only[i]) == 0) {
			show_status = 1;
		} else {
			ast_log(LOG_WARNING, "Unrecognized info section '%s'\n",
				args->only[i]);
		}
	}

	json = ast_json_object_create();

	if (show_build) {
		res |= ast_json_object_set(json, "build",
			ast_json_pack(
				"{ s: s, s: s, s: s,"
				"  s: s, s: s, s: s }",

				"os", ast_build_os,
				"kernel", ast_build_kernel,
				"machine", ast_build_machine,

				"options", AST_BUILDOPTS,
				"date", ast_build_date,
				"user", ast_build_user));
	}

	if (show_system) {
		char eid_str[128];

		ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

		res |= ast_json_object_set(json, "system",
			ast_json_pack("{ s: s, s: s }",
				"version", ast_get_version(),
				"entity_id", eid_str));
	}

	if (show_config) {
		struct ast_json *config = ast_json_pack(
			"{ s: s, s: s,"
			" s: { s: s, s: s } }",

			"name", ast_config_AST_SYSTEM_NAME,
			"default_language", ast_defaultlanguage,

			"setid",
			"user", ast_config_AST_RUN_USER,
			"group", ast_config_AST_RUN_GROUP);

		res |= ast_json_object_set(json, "config", config);

		if (ast_option_maxcalls) {
			res |= ast_json_object_set(config, "max_channels",
				ast_json_integer_create(ast_option_maxcalls));
		}

		if (ast_option_maxfiles) {
			res |= ast_json_object_set(config, "max_open_files",
				ast_json_integer_create(ast_option_maxfiles));
		}

		if (ast_option_maxload) {
			res |= ast_json_object_set(config, "max_load",
				ast_json_real_create(ast_option_maxload));
		}
	}

	if (show_status) {
		res |= ast_json_object_set(json, "status",
			ast_json_pack("{ s: o, s: o }",
				"startup_time",
				ast_json_timeval(ast_startuptime, NULL),
				"last_reload_time",
				ast_json_timeval(ast_lastreloadtime, NULL)));
	}

	if (res != 0) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

/*!
 * \brief Process module information and append to a json array
 * \param module Resource name
 * \param description Resource description
 * \param usecnt Resource use count
 * \param status Resource running status
 * \param like
 * \param support_level Resource support level
 * \param module_data_list Resource array
 *
 * \retval 0 if no resource exists
 * \retval 1 if resource exists
 */
static int process_module_list(const char *module, const char *description, int usecnt,
                               const char *status, const char *like,
                               enum ast_module_support_level support_level, void *module_data_list)
{
	struct ast_json *module_info;

	module_info = ast_json_pack("{s: s, s: s, s: i, s: s, s: s}",
                              "name", module,
                              "description", description,
                              "use_count", usecnt,
                              "status", status,
                              "support_level", ast_module_support_level_to_string(support_level));
	if (!module_info) {
		return 0;
	}
	ast_json_array_append(module_data_list, module_info);
	return 1;
}

void ast_ari_asterisk_list_modules(struct ast_variable *headers,
	struct ast_ari_asterisk_list_modules_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	json = ast_json_array_create();
	ast_update_module_list_data(&process_module_list, NULL, json);

	ast_ari_response_ok(response, json);
}

/*!
 * \brief Identify module by name and process resource information
 * \param module Resource name
 * \param description Resource description
 * \param usecnt Resource use count
 * \param status Resource running status
 * \param like
 * \param support_level Resource support level
 * \param data JSON body for resource
 * \param condition Name to match resource to
 *
 * \retval 0 if no resource exists
 * \retval 1 if resource exists
 */
static int identify_module(const char *module, const char *description, int usecnt,
                           const char *status, const char *like,
                           enum ast_module_support_level support_level, void *data,
                           const char *condition)
{
	int json_obj_set = 0;

	if (strcmp(condition, module) != 0) {
		return 0;
	}

	json_obj_set += ast_json_object_set(data, "name", ast_json_string_create(module));
	json_obj_set += ast_json_object_set(data, "description", ast_json_string_create(description));
	json_obj_set += ast_json_object_set(data, "use_count", ast_json_integer_create(usecnt));
	json_obj_set += ast_json_object_set(data, "status", ast_json_string_create(status));
	json_obj_set += ast_json_object_set(data, "support_level", ast_json_string_create(
	                                    ast_module_support_level_to_string(support_level)));

	if (json_obj_set != 0) {
		return 0;
	}

	return 1;
}

void ast_ari_asterisk_get_module(struct ast_variable *headers,
	struct ast_ari_asterisk_get_module_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;
	int module_retrieved = 0;

	ast_assert(response != NULL);

	if (!ast_module_check(args->module_name)) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Module could not be found in running modules");
		return;
	}

	json = ast_json_object_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	module_retrieved = ast_update_module_list_condition(&identify_module, NULL, json,
	                                                    args->module_name);
	if (!module_retrieved) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module information could not be retrieved");
		return;
	}

	ast_ari_response_ok(response, json);
}

void ast_ari_asterisk_load_module(struct ast_variable *headers,
	struct ast_ari_asterisk_load_module_args *args,
	struct ast_ari_response *response)
{
	enum ast_module_load_result load_result;

	ast_assert(response != NULL);

	if (ast_module_check(args->module_name)) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module is already loaded");
		return;
	}

	load_result = ast_load_resource(args->module_name);

	if (load_result == AST_MODULE_LOAD_DECLINE) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module load declined");
		return;
	} else if (load_result == AST_MODULE_LOAD_SKIP) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module was skipped");
		return;
	} else if (load_result == AST_MODULE_LOAD_FAILURE) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module could not be loaded properly");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_asterisk_unload_module(struct ast_variable *headers,
	struct ast_ari_asterisk_unload_module_args *args,
	struct ast_ari_response *response)
{
	int unload_result;
	enum ast_module_unload_mode unload_mode = AST_FORCE_SOFT;

	ast_assert(response != NULL);

	if (!ast_module_check(args->module_name)) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Module not found in running modules");
		return;
	}

	unload_result = ast_unload_resource(args->module_name, unload_mode);

	if (unload_result != 0) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module could not be unloaded");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_asterisk_reload_module(struct ast_variable *headers,
	struct ast_ari_asterisk_reload_module_args *args,
	struct ast_ari_response *response)
{
	enum ast_module_reload_result reload_result;

	ast_assert(response != NULL);

	if (!ast_module_check(args->module_name)) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Module not found in running modules");
		return;
	}

	reload_result = ast_module_reload(args->module_name);

	if (reload_result == AST_MODULE_RELOAD_NOT_FOUND) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Module could not be found");
		return;
	} else if (reload_result == AST_MODULE_RELOAD_ERROR) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"An unknown error occurred while reloading the module");
		return;
	} else if (reload_result == AST_MODULE_RELOAD_IN_PROGRESS) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Another reload is currently in progress");
		return;
	} else if (reload_result == AST_MODULE_RELOAD_UNINITIALIZED) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module has not been initialized");
		return;
	} else if (reload_result == AST_MODULE_RELOAD_NOT_IMPLEMENTED) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Module does not support reloading");
		return;
	} else if (reload_result == AST_MODULE_RELOAD_QUEUED) {
		ast_ari_response_accepted(response);
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_asterisk_get_global_var(struct ast_variable *headers,
	struct ast_ari_asterisk_get_global_var_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, tmp, NULL, ast_free);

	const char *value;

	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	tmp = ast_str_create(32);
	if (!tmp) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	value = ast_str_retrieve_variable(&tmp, 0, NULL, NULL, args->variable);

	if (!(json = ast_json_pack("{s: s}", "value", S_OR(value, "")))) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_asterisk_set_global_var(struct ast_variable *headers,
	struct ast_ari_asterisk_set_global_var_args *args,
	struct ast_ari_response *response)
{
	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	pbx_builtin_setvar_helper(NULL, args->variable, args->value);

	ast_ari_response_no_content(response);
}
