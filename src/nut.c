/**
 * collectd - src/nut.c
 * Copyright (C) 2007  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#if HAVE_UPSCLIENT_H
# include <upsclient.h>
# define NUT_HAVE_READ 1
#else
# define NUT_HAVE_READ 0
#endif

static data_source_t data_source_current[1] =
{
  {"value", DS_TYPE_GAUGE, NAN, NAN}
};

static data_set_t ds_current =
{
  "current", 1, data_source_current
};

static data_source_t data_source_humidity[1] =
{
  {"value", DS_TYPE_GAUGE, 0.0, 100.0}
};

static data_set_t ds_humidity =
{
  "humidity", 1, data_source_humidity
};

static data_source_t data_source_power[1] =
{
  {"value", DS_TYPE_GAUGE, 0.0, NAN}
};

static data_set_t ds_power =
{
  "power", 1, data_source_power
};

static data_source_t data_source_voltage[1] =
{
  {"value", DS_TYPE_GAUGE, NAN, NAN}
};

static data_set_t ds_voltage =
{
  "voltage", 1, data_source_voltage
};

static data_source_t data_source_percent[1] =
{
  {"percent", DS_TYPE_GAUGE, 0, 100.1}
};

static data_set_t ds_percent =
{
  "percent", 1, data_source_percent
};

static data_source_t data_source_timeleft[1] =
{
  {"timeleft", DS_TYPE_GAUGE, 0, 100.0}
};

static data_set_t ds_timeleft =
{
  "timeleft", 1, data_source_timeleft
};

static data_source_t data_source_temperature[1] =
{
  {"value", DS_TYPE_GAUGE, -273.15, NAN}
};

static data_set_t ds_temperature =
{
  "temperature", 1, data_source_temperature
};

static data_source_t data_source_frequency[1] =
{
  {"frequency", DS_TYPE_GAUGE, 0, NAN}
};

static data_set_t ds_frequency =
{
  "frequency", 1, data_source_frequency
};

#if NUT_HAVE_READ
struct nut_ups_s;
typedef struct nut_ups_s nut_ups_t;
struct nut_ups_s
{
  UPSCONN    conn;
  char      *upsname;
  char      *hostname;
  int        port;
  nut_ups_t *next;
};

static nut_ups_t *upslist_head = NULL;

static pthread_mutex_t read_lock = PTHREAD_MUTEX_INITIALIZER;
static int read_busy = 0;

static const char *config_keys[] =
{
  "UPS"
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static void free_nut_ups_t (nut_ups_t *ups)
{
    upscli_disconnect (&ups->conn);
    sfree (ups->hostname);
    sfree (ups->upsname);
    sfree (ups);
} /* void free_nut_ups_t */

static int nut_add_ups (const char *name)
{
  nut_ups_t *ups;
  int status;

  DEBUG ("nut plugin: nut_add_ups (name = %s);", name);

  ups = (nut_ups_t *) malloc (sizeof (nut_ups_t));
  if (ups == NULL)
  {
    ERROR ("nut plugin: nut_add_ups: malloc failed.");
    return (1);
  }
  memset (ups, '\0', sizeof (nut_ups_t));

  status = upscli_splitname (name, &ups->upsname, &ups->hostname,
      &ups->port);
  if (status != 0)
  {
    ERROR ("nut plugin: nut_add_ups: upscli_splitname (%s) failed.", name);
    free_nut_ups_t (ups);
    return (1);
  }

  status = upscli_connect (&ups->conn, ups->hostname, ups->port,
      UPSCLI_CONN_TRYSSL);
  if (status != 0)
  {
    ERROR ("nut plugin: nut_add_ups: upscli_connect (%s, %i) failed: %s",
	ups->hostname, ups->port, upscli_strerror (&ups->conn));
    free_nut_ups_t (ups);
    return (1);
  }

  if (upslist_head == NULL)
    upslist_head = ups;
  else
  {
    nut_ups_t *last = upslist_head;
    while (last->next != NULL)
      last = last->next;
    last->next = ups;
  }

  return (0);
} /* int nut_add_ups */

static int nut_config (const char *key, const char *value)
{
  if (strcasecmp (key, "UPS") == 0)
    return (nut_add_ups (value));
  else
    return (-1);
} /* int nut_config */

static void nut_submit (nut_ups_t *ups, const char *type,
    const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  vl.time = time (NULL);
  strncpy (vl.host,
      (strcasecmp (ups->hostname, "localhost") == 0)
      ? hostname_g
      : ups->hostname,
      sizeof (vl.host));
  strcpy (vl.plugin, "nut");
  strncpy (vl.plugin_instance, ups->upsname, sizeof (vl.plugin_instance));
  strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  vl.host[sizeof (vl.host) - 1] = '\0';
  vl.plugin_instance[sizeof (vl.plugin_instance) - 1] = '\0';
  vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';

  plugin_dispatch_values (type, &vl);
} /* void nut_submit */

static int nut_read_one (nut_ups_t *ups)
{
  const char *query[3] = {"VAR", ups->upsname, NULL};
  unsigned int query_num = 2;
  char **answer;
  unsigned int answer_num;
  int status;

  /* nut plugin: nut_read_one: upscli_list_start (adpos) failed: Protocol
   * error */
  status = upscli_list_start (&ups->conn, query_num, query);
  if (status != 0)
  {
    ERROR ("nut plugin: nut_read_one: upscli_list_start (%s) failed: %s",
	ups->upsname, upscli_strerror (&ups->conn));
    return (-1);
  }

  while ((status = upscli_list_next (&ups->conn, query_num, query,
	  &answer_num, &answer)) == 1)
  {
    char  *key;
    double value;

    if (answer_num < 4)
      continue;

    key = answer[2];
    value = atof (answer[3]);

    if (strncmp ("ambient.", key, 8) == 0)
    {
      if (strcmp ("ambient.humidity", key) == 0)
	nut_submit (ups, "humidity", "ambient", value);
      else if (strcmp ("ambient.temperature", key) == 0)
	nut_submit (ups, "temperature", "ambient", value);
    }
    else if (strncmp ("battery.", key, 8) == 0)
    {
      if (strcmp ("battery.charge", key) == 0)
	nut_submit (ups, "percent", "charge", value);
      else if (strcmp ("battery.current", key) == 0)
	nut_submit (ups, "current", "battery", value);
      else if (strcmp ("battery.runtime", key) == 0)
	nut_submit (ups, "timeleft", "battery", value);
      else if (strcmp ("battery.temperature", key) == 0)
	nut_submit (ups, "temperature", "battery", value);
      else if (strcmp ("battery.voltage", key) == 0)
	nut_submit (ups, "voltage", "battery", value);
    }
    else if (strncmp ("input.", key, 6) == 0)
    {
      if (strcmp ("input.frequency", key) == 0)
	nut_submit (ups, "frequency", "input", value);
      else if (strcmp ("input.voltage", key) == 0)
	nut_submit (ups, "voltage", "input", value);
    }
    else if (strncmp ("output.", key, 7) == 0)
    {
      if (strcmp ("output.current", key) == 0)
	nut_submit (ups, "current", "output", value);
      else if (strcmp ("output.frequency", key) == 0)
	nut_submit (ups, "frequency", "output", value);
      else if (strcmp ("output.voltage", key) == 0)
	nut_submit (ups, "voltage", "output", value);
    }
    else if (strncmp ("ups.", key, 4) == 0)
    {
      if (strcmp ("ups.load", key) == 0)
	nut_submit (ups, "percent", "load", value);
      else if (strcmp ("ups.power", key) == 0)
	nut_submit (ups, "power", "ups", value);
      else if (strcmp ("ups.temperature", key) == 0)
	nut_submit (ups, "temperature", "ups", value);
    }
  } /* while (upscli_list_next) */

  return (0);
} /* int nut_read_one */

static int nut_read (void)
{
  nut_ups_t *ups;
  int success = 0;

  pthread_mutex_lock (&read_lock);
  success = read_busy;
  read_busy = 1;
  pthread_mutex_unlock (&read_lock);

  if (success != 0)
    return (0);

  for (ups = upslist_head; ups != NULL; ups = ups->next)
    if (nut_read_one (ups) == 0)
      success++;

  pthread_mutex_lock (&read_lock);
  read_busy = 0;
  pthread_mutex_unlock (&read_lock);

  return ((success != 0) ? 0 : -1);
} /* int nut_read */

static int nut_shutdown (void)
{
  nut_ups_t *this;
  nut_ups_t *next;

  this = upslist_head;
  while (this != NULL)
  {
    next = this->next;
    free_nut_ups_t (this);
    this = next;
  }

  return (0);
} /* int nut_shutdown */
#endif /* NUT_HAVE_READ */

void module_register (modreg_e load)
{
  if (load & MR_DATASETS)
  {
    plugin_register_data_set (&ds_current);
    plugin_register_data_set (&ds_humidity);
    plugin_register_data_set (&ds_power);
    plugin_register_data_set (&ds_voltage);
    plugin_register_data_set (&ds_percent);
    plugin_register_data_set (&ds_timeleft);
    plugin_register_data_set (&ds_temperature);
    plugin_register_data_set (&ds_frequency);
  }

#if NUT_HAVE_READ
  if (load & MR_READ)
  {
    plugin_register_config ("nut", nut_config, config_keys, config_keys_num);
    plugin_register_read ("nut", nut_read);
    plugin_register_shutdown ("nut", nut_shutdown);
  }
#endif
} /* void module_register */

/* vim: set sw=2 ts=8 sts=2 tw=78 : */