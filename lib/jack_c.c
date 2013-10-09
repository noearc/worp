
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#define MAX_PORTS 4
#define RB_SIZE 4096
	
#define sample_t jack_default_audio_sample_t


struct port {
	int flags;
	jack_port_t *port;
	jack_ringbuffer_t *rb;
	struct port *next;
};


struct group {
	int id;
	int fd;
	char *name;
	struct port *port_list;
	struct group *next;
};


struct midi {
	jack_port_t *port;
	int fd;
	struct midi *next;
};


struct jack {
	int fd;
	int direction;
	jack_client_t *client;
	jack_port_t *port[MAX_PORTS];
	jack_ringbuffer_t *rb[MAX_PORTS];
	jack_port_t *midi_in;

	int group_seq;
	struct group *group_list;
	struct midi *midi_list;
};


static int process2(jack_nframes_t nframes, void *arg)
{
	struct jack *jack = arg;
	struct group *group = jack->group_list;
	int len = nframes * sizeof(sample_t);

	while(group) {

		int need_data = 0;

		struct port *port = group->port_list;
		while(port) {

			jack_ringbuffer_t *rb = port->rb;
			jack_port_t *p = port->port;

			if(port->flags == JackPortIsOutput) {

				int avail = jack_ringbuffer_read_space(rb);

				if(avail < len * 2) need_data = 1;

				if(avail >= len) {
					sample_t *buf = jack_port_get_buffer(p, nframes);
					int r = jack_ringbuffer_read(rb, (void *)buf, len);
					if(0 && r != len) printf("underrun\n");
				}

			}

			if(port->flags == JackPortIsInput) {

				if(jack_ringbuffer_write_space(rb) >= len) {
					sample_t *buf = jack_port_get_buffer(p, nframes);
					int r = jack_ringbuffer_write(rb, (void *)buf, len);
					if(0 && r != len) printf("overrun\n");
				}
			}

			port = port->next;
		}

		if(need_data) {
			write(group->fd, " ", 1);
		}
				

		group = group->next;
	}

	struct midi *midi = jack->midi_list;

	while(midi) {
		void *buf = jack_port_get_buffer(midi->port, nframes);
		int n = jack_midi_get_event_count(buf);
		int i;
		for(i=0; i<n; i++) {
			jack_midi_event_t ev;
			jack_midi_event_get(&ev, buf, i);
			write(midi->fd, ev.buffer, ev.size);
		}
		midi = midi->next;
	}

	return 0;
}


static int l_new(lua_State *L)
{
	const char *client_name = luaL_checkstring(L, 1);
	jack_options_t options = JackNullOption;
	jack_status_t status;
	
	struct jack *jack = lua_newuserdata(L, sizeof *jack);
	memset(jack, 0, sizeof *jack);
        lua_getfield(L, LUA_REGISTRYINDEX, "jack_c");
	lua_setmetatable(L, -2);
	
	jack->client = jack_client_open (client_name, options, &status, NULL);
	if (jack->client == NULL) {
		lua_pushnil(L);
		lua_pushfstring(L, "Error creating jack client, status = %x", status);
		return 2;
	}
	
	jack_set_process_callback(jack->client, process2, jack);
	//jack_activate (jack->client);
	
	lua_pushnumber(L, jack_get_sample_rate(jack->client));
	lua_pushnumber(L, jack_get_buffer_size(jack->client));
        return 3;
}
	

static int l_add_group(lua_State *L)
{
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	const char *name = luaL_checkstring(L, 2);
	int n_in = luaL_checknumber(L, 3);
	int n_out = luaL_checknumber(L, 4);
	int i;
	int fd[2];

	pipe(fd);

	char pname[64];
	struct group *group = calloc(sizeof *group, 1);
	group->name = strdup(name);
	group->id = jack->group_seq ++;
	group->fd = fd[1];

	for(i=0; i<n_in + n_out; i++) {

		struct port *port = calloc(sizeof *port, 1);
		port->flags = (i < n_in) ? JackPortIsInput : JackPortIsOutput;

		snprintf(pname, sizeof(pname), "%s-%s-%d", name, (i<n_in) ? "in" : "out", (i<n_in) ? i+1 : i - n_in+1);

		port->port = jack_port_register(jack->client, pname, JACK_DEFAULT_AUDIO_TYPE, port->flags, 0);
		port->rb = jack_ringbuffer_create(RB_SIZE);

		port->next = group->port_list;
		group->port_list = port;
	}

	group->next = jack->group_list;
	jack->group_list = group;
	
	jack_activate (jack->client);

	lua_pushnumber(L, fd[0]);
	lua_pushnumber(L, group->id);
	return 2;
}


static int l_add_midi(lua_State *L)
{
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	const char *name = luaL_checkstring(L, 2);
	int fd[2];
	char pname[64];

	pipe(fd);

	struct midi *midi = calloc(sizeof *midi, 1);

	snprintf(pname, sizeof(pname), "%s-in", name);
	midi->port = jack_port_register(jack->client, pname, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	midi->fd = fd[1];

	midi->next = jack->midi_list;
	jack->midi_list = midi;

	lua_pushnumber(L, fd[0]);
	return 1;
}



static int l_write(lua_State *L)
{
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	int gid = luaL_checknumber(L, 2);
	int n = 3;

	struct group *group = jack->group_list;
	while(group) {
		if(group->id == gid) {
			struct port *port = group->port_list;

			while(port) {
				if(port->flags == JackPortIsOutput) {
					sample_t s = lua_tonumber(L, n++);
					if(jack_ringbuffer_write_space(port->rb) >= sizeof s) {
						jack_ringbuffer_write(port->rb, (void *)&s, sizeof s);
					}
				}
				port = port->next;
			}
		}
		group = group->next;
	}


	return 0;
}


static int l_read(lua_State *L)
{
	int n = 0;
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	int gid = luaL_checknumber(L, 2);

	struct group *group = jack->group_list;
	while(group) {
		if(group->id == gid) {
			struct port *port = group->port_list;

			while(port) {
				if(port->flags == JackPortIsInput) {
					sample_t s = 0;
					if(jack_ringbuffer_read_space(port->rb) >= sizeof s) {
						jack_ringbuffer_read(port->rb, (void *)&s, sizeof s);

					}
					lua_pushnumber(L, s);
					n++;
				}
				port = port->next;
			}
		}
		group = group->next;
	}

	return n;
}


static int l_connect(lua_State *L)
{
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	const char *p1 = luaL_checkstring(L, 2);
	const char *p2 = luaL_checkstring(L, 3);

	int r = jack_connect(jack->client, p1, p2);
	lua_pushnumber(L, r);
	return 1;
}


static int l_disconnect(lua_State *L)
{
	struct jack *jack = luaL_checkudata(L, 1, "jack_c");
	const char *p1 = luaL_checkstring(L, 2);
	const char *p2 = luaL_checkstring(L, 3);

	int r = jack_disconnect(jack->client, p1, p2);
	lua_pushnumber(L, r);
	return 1;
}


static struct luaL_Reg jack_table[] = {

        { "new",		l_new },
        { "add_group",		l_add_group },
        { "add_midi",		l_add_midi },

	{ "write",		l_write },
	{ "read",		l_read },
	{ "connect",		l_connect },
	{ "disconnect",		l_disconnect },

        { NULL },
};


int luaopen_jack_c(lua_State *L)
{
	luaL_newmetatable(L, "jack_c");
        luaL_register(L, "jack_c", jack_table);

        return 0;
}

/*
 * End
 */
