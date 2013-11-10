/*
 * This file is part of the libsigrokdecode project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libsigrokdecode.h" /* First, so we avoid a _POSIX_C_SOURCE warning. */
#include "libsigrokdecode-internal.h"
#include "config.h"
#include <inttypes.h>

extern struct key_string *srd_meta_key_str;

/* This is only used for nicer srd_dbg() output. */
static const char *OUTPUT_TYPES[] = {
	"OUTPUT_ANN",
	"OUTPUT_PYTHON",
	"OUTPUT_BINARY",
	"OUTPUT_META",
};

static int convert_annotation(struct srd_decoder_inst *di, PyObject *obj,
		struct srd_proto_data *pdata)
{
	PyObject *py_tmp;
	struct srd_pd_output *pdo;
	struct srd_proto_data_annotation *pda;
	int ann_format;
	char **ann_text;

	/* Should be a list of [annotation format, [string, ...]]. */
	if (!PyList_Check(obj) && !PyTuple_Check(obj)) {
		srd_err("Protocol decoder %s submitted %s instead of list.",
			di->decoder->name, obj->ob_type->tp_name);
		return SRD_ERR_PYTHON;
	}

	/* Should have 2 elements. */
	if (PyList_Size(obj) != 2) {
		srd_err("Protocol decoder %s submitted annotation list with "
			"%d elements instead of 2", di->decoder->name,
			PyList_Size(obj));
		return SRD_ERR_PYTHON;
	}

	/*
	 * The first element should be an integer matching a previously
	 * registered annotation format.
	 */
	py_tmp = PyList_GetItem(obj, 0);
	if (!PyLong_Check(py_tmp)) {
		srd_err("Protocol decoder %s submitted annotation list, but "
			"first element was not an integer.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}
	ann_format = PyLong_AsLong(py_tmp);
	if (!(pdo = g_slist_nth_data(di->decoder->annotations, ann_format))) {
		srd_err("Protocol decoder %s submitted data to unregistered "
			"annotation format %d.", di->decoder->name, ann_format);
		return SRD_ERR_PYTHON;
	}

	/* Second element must be a list. */
	py_tmp = PyList_GetItem(obj, 1);
	if (!PyList_Check(py_tmp)) {
		srd_err("Protocol decoder %s submitted annotation list, but "
			"second element was not a list.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}
	if (py_strlist_to_char(py_tmp, &ann_text) != SRD_OK) {
		srd_err("Protocol decoder %s submitted annotation list, but "
			"second element was malformed.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}

	if (!(pda = g_try_malloc(sizeof(struct srd_proto_data_annotation))))
		return SRD_ERR_MALLOC;
	pda->ann_format = ann_format;
	pda->ann_text = ann_text;
	pdata->data = pda;

	return SRD_OK;
}

static int convert_binary(struct srd_decoder_inst *di, PyObject *obj,
		struct srd_proto_data *pdata)
{
	struct srd_proto_data_binary *pdb;
	PyObject *py_tmp;
	Py_ssize_t size;
	int bin_class;
	char *class_name, *buf;

	/* Should be a tuple of (binary class, bytes). */
	if (!PyTuple_Check(obj)) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY with "
				"%s instead of tuple.", di->decoder->name,
				obj->ob_type->tp_name);
		return SRD_ERR_PYTHON;
	}

	/* Should have 2 elements. */
	if (PyTuple_Size(obj) != 2) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY tuple "
				"with %d elements instead of 2", di->decoder->name,
				PyList_Size(obj));
		return SRD_ERR_PYTHON;
	}

	/* The first element should be an integer. */
	py_tmp = PyTuple_GetItem(obj, 0);
	if (!PyLong_Check(py_tmp)) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY tuple, "
			"but first element was not an integer.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}
	bin_class = PyLong_AsLong(py_tmp);
	if (!(class_name = g_slist_nth_data(di->decoder->binary, bin_class))) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY with "
			"unregistered binary class %d.", di->decoder->name, bin_class);
		return SRD_ERR_PYTHON;
	}

	/* Second element should be bytes. */
	py_tmp = PyTuple_GetItem(obj, 1);
	if (!PyBytes_Check(py_tmp)) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY tuple, "
			"but second element was not bytes.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}

	/* Consider an empty set of bytes a bug. */
	if (PyBytes_Size(py_tmp) == 0) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_BINARY "
				"with empty data set.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}

	if (!(pdb = g_try_malloc(sizeof(struct srd_proto_data_binary))))
		return SRD_ERR_MALLOC;
	if (PyBytes_AsStringAndSize(py_tmp, &buf, &size) == -1)
		return SRD_ERR_PYTHON;
	pdb->bin_class = bin_class;
	pdb->size = size;
	if (!(pdb->data = g_try_malloc(pdb->size)))
		return SRD_ERR_MALLOC;
	memcpy((void *)pdb->data, (const void *)buf, pdb->size);
	pdata->data = pdb;

	return SRD_OK;
}

static int convert_meta(struct srd_decoder_inst *di, PyObject *obj,
		struct srd_proto_data *pdata)
{
	struct srd_proto_data_meta *pdm;
	const struct srd_meta_info *mi;
	PyObject *py_keys, *py_key, *py_value;
	uint64_t intvalue;
	int key, ret, i;

	/* Should be a dictionary. */
	if (!PyDict_Check(obj)) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_META with "
				"%s instead of a dictionary.",
			di->decoder->name, obj->ob_type->tp_name);
		return SRD_ERR_PYTHON;
	}

	/* With at least one keypair in it. */
	if (PyDict_Size(obj) == 0) {
		srd_err("Protocol decoder %s submitted SRD_OUTPUT_META with "
				"empty dictionary.", di->decoder->name);
		return SRD_ERR_PYTHON;
	}

	ret = SRD_OK;
	py_keys = PyDict_Keys(obj);
	for (i = 0; i < PyList_Size(py_keys); i++) {
		py_key = PyList_GetItem(py_keys, i);
		if (PyLong_Check(py_key)) {
			/*
			 * The key is an integer, so it has to be a well-known
			 * enumerated value.
			 */
			key = PyLong_AsLong(py_key);
			mi = srd_meta_info_get(key);
			if (!mi) {
				srd_err("Protocol decoder %s submitted SRD_OUTPUT_META with "
						"an unknown key %d.", di->decoder->name, key);
				goto err_out;
			}
			switch (key) {
			case SRD_META_BITRATE:
				/* Value must be an integer. */
				py_value = PyDict_GetItem(obj, py_key);
				if (!PyLong_Check(py_value)) {
					srd_err("Protocol decoder %s submitted well-known "
							"key %s, but the value wasn't an integer.",
							di->decoder->name, mi->keyname);
					ret = SRD_ERR_PYTHON;
					goto err_out;
				}
				if (!(pdm = g_try_malloc(sizeof(struct srd_proto_data_meta))))
					return SRD_ERR_MALLOC;
				pdm->key = key;
				intvalue = PyLong_AsUnsignedLongLong(py_value);
				pdm->value = g_variant_new_uint64(intvalue);
				pdata->data = pdm;
				break;
			default:
				/* This switch is out of sync with the meta_info table. */
				srd_err("Protocol decoder %s submitted SRD_OUTPUT_META with "
						"an unknown key %d.", di->decoder->name, key);
					ret = SRD_ERR_BUG;
					goto err_out;
			}
		}
	}

err_out:
	Py_XDECREF(py_keys);

	return ret;
}

static PyObject *Decoder_put(PyObject *self, PyObject *args)
{
	GSList *l;
	PyObject *py_data, *py_res;
	struct srd_decoder_inst *di, *next_di;
	struct srd_pd_output *pdo;
	struct srd_proto_data *pdata;
	uint64_t start_sample, end_sample;
	int output_id;
	struct srd_pd_callback *cb;

	if (!(di = srd_inst_find_by_obj(NULL, self))) {
		/* Shouldn't happen. */
		srd_dbg("put(): self instance not found.");
		return NULL;
	}

	if (!PyArg_ParseTuple(args, "KKiO", &start_sample, &end_sample,
	    &output_id, &py_data)) {
		/*
		 * This throws an exception, but by returning NULL here we let
		 * Python raise it. This results in a much better trace in
		 * controller.c on the decode() method call.
		 */
		return NULL;
	}

	if (!(l = g_slist_nth(di->pd_output, output_id))) {
		srd_err("Protocol decoder %s submitted invalid output ID %d.",
			di->decoder->name, output_id);
		return NULL;
	}
	pdo = l->data;

	srd_spew("Instance %s put %" PRIu64 "-%" PRIu64 " %s on oid %d.",
		 di->inst_id, start_sample, end_sample,
		 OUTPUT_TYPES[pdo->output_type], output_id);

	if (!(pdata = g_try_malloc0(sizeof(struct srd_proto_data)))) {
		srd_err("Failed to g_malloc() struct srd_proto_data.");
		return NULL;
	}
	pdata->start_sample = start_sample;
	pdata->end_sample = end_sample;
	pdata->pdo = pdo;

	switch (pdo->output_type) {
	case SRD_OUTPUT_ANN:
		/* Annotations are only fed to callbacks. */
		if ((cb = srd_pd_output_callback_find(di->sess, pdo->output_type))) {
			/* Convert from PyDict to srd_proto_data_annotation. */
			if (convert_annotation(di, py_data, pdata) != SRD_OK) {
				/* An error was already logged. */
				break;
			}
			cb->cb(pdata, cb->cb_data);
		}
		break;
	case SRD_OUTPUT_PYTHON:
		for (l = di->next_di; l; l = l->next) {
			next_di = l->data;
			/* TODO: Is this needed? */
			Py_XINCREF(next_di->py_inst);
			srd_spew("Sending %d-%d to instance %s",
				 start_sample, end_sample,
				 next_di->inst_id);
			if (!(py_res = PyObject_CallMethod(
			    next_di->py_inst, "decode", "KKO", start_sample,
			    end_sample, py_data))) {
				srd_exception_catch("Calling %s decode(): ",
						    next_di->inst_id);
			}
			Py_XDECREF(py_res);
		}
		break;
	case SRD_OUTPUT_BINARY:
		if ((cb = srd_pd_output_callback_find(di->sess, pdo->output_type))) {
			/* Convert from PyDict to srd_proto_data_binary. */
			if (convert_binary(di, py_data, pdata) != SRD_OK) {
				/* An error was already logged. */
				break;
			}
			cb->cb(pdata, cb->cb_data);
		}
		break;
	case SRD_OUTPUT_META:
		if ((cb = srd_pd_output_callback_find(di->sess, pdo->output_type))) {
			/* Convert from PyDict to srd_proto_data_meta. */
			if (convert_meta(di, py_data, pdata) != SRD_OK) {
				/* An error was already logged. */
				break;
			}
			cb->cb(pdata, cb->cb_data);
		}
		break;
	default:
		srd_err("Protocol decoder %s submitted invalid output type %d.",
			di->decoder->name, pdo->output_type);
		break;
	}

	g_free(pdata);

	Py_RETURN_NONE;
}

static PyObject *Decoder_add(PyObject *self, PyObject *args)
{
	PyObject *ret;
	struct srd_decoder_inst *di;
	char *proto_id;
	int output_type, pdo_id;

	if (!(di = srd_inst_find_by_obj(NULL, self))) {
		PyErr_SetString(PyExc_Exception, "decoder instance not found");
		return NULL;
	}

	if (!PyArg_ParseTuple(args, "is", &output_type, &proto_id)) {
		/* Let Python raise this exception. */
		return NULL;
	}

	pdo_id = srd_inst_pd_output_add(di, output_type, proto_id);
	if (pdo_id < 0)
		Py_RETURN_NONE;
	else
		ret = Py_BuildValue("i", pdo_id);

	return ret;
}

static PyMethodDef Decoder_methods[] = {
	{"put", Decoder_put, METH_VARARGS,
	 "Accepts a dictionary with the following keys: startsample, endsample, data"},
	{"add", Decoder_add, METH_VARARGS, "Create a new output stream"},
	{NULL, NULL, 0, NULL}
};

/** @cond PRIVATE */
SRD_PRIV PyTypeObject srd_Decoder_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "sigrokdecode.Decoder",
	.tp_basicsize = sizeof(srd_Decoder),
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_doc = "sigrok Decoder base class",
	.tp_methods = Decoder_methods,
};
/** @endcond */
