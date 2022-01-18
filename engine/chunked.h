#define CHUNKED_MAGIC "CHUNKED"
#define CHUNKED_VERSION 2

#define MAXSIZE_COMPRESSED (1<<16)
#define MAXSIZE_COMPRESSBOUND (1<<20)

/*
	MAGIC VERSION
	MAPSCALE NUMENTS NUMVARS NUMCHUNKS
	X Y Z TYPE ATTR1 ATTR2 ATTR3 ATTR4 ATTR5 <-- foreach ent
	TYPE NAME VAL                            <-- foreach var
	GRIDSCALE X Y Z I J K UNPACKLEN PACKLEN  <-- foreach chunk
*/
/*
	100 = RIGHT
	010 = FRONT
	001 = TOP

	000 000
	001 010
	010 100
	011 110
	100
	101
	110
	111
*/

void SELECTION_SET_SUBCHUNK(selinfo &sel, int which) {
	sel.grid >>= 1;
	const int i = which >> 2, j = (which >> 1) & 1, k = which & 1;
	sel.o.x += i * sel.grid; sel.o.y += j * sel.grid; sel.o.z += k * sel.grid;
}

// take a selection with the starting point in the origin of a cube and turn it into a plane with half the volume
void SELECTION_SET_PLANE(selinfo &sel, int axis, int which) {
	sel.grid >>= 1;
	if(axis == 0) {
		if(which) sel.o.x += sel.grid;
		sel.s = ivec(1, 2, 2);
	} else if(axis == 1) {
		if(which) sel.o.y += sel.grid;
		sel.s = ivec(2, 1, 2);
	} else if(axis == 2) {
		if(which) sel.o.z += sel.grid;
		sel.s = ivec(2, 2, 1);
	}
}

void SELECTION_SET_PILLAR(selinfo &sel, int axis, int which) {
	sel.grid >>= 1;
	if(axis == 0) {
		// left to right
		// first bit Y, second bit Z
		const int y = (which >> 1) & 1;
		const int z = which & 1;
		if(y) sel.o.y += sel.grid;
		if(z) sel.o.z += sel.grid;
		sel.s = ivec(2,1,1);
	} else if(axis == 1) {
		// back to front
		// first bit X, second bit Z
		const int x = (which >> 1) & 1;
		const int z = which & 1;
		if(x) sel.o.x += sel.grid;
		if(z) sel.o.z += sel.grid;
		sel.s = ivec(1,2,1);
	} else if(axis == 2) {
		// bottom to top
		// first bit X, second bit Y
		const int x = (which >> 1) & 1;
		const int y = which & 1;
		if(x) sel.o.x += sel.grid;
		if(y) sel.o.y += sel.grid;
		sel.s = ivec(1,1,2);
	}
}

// get subchunk indexes for a plane
void GET_PLANE_INDEXES(vector<int> &indexes, int axis, int which) {
	indexes.setsize(0);
	loopi(4) indexes.add(i);
	if(axis == 0) {
		loopi(4) indexes[i] = which
			? (indexes[i] | 4)
			: (indexes[i] & 3);
	} else if(axis == 1) {
		loopi(4) indexes[i] |= (indexes[i] << 1) & 4;
		loopi(4) indexes[i] = which
			? (indexes[i] & 5)
			: (indexes[i] | 2);
	} else { // axis == 2
		loopi(4) indexes[i] <<= 1;
		loopi(4) indexes[i] = which
			? (indexes[i] | 1)
			: (indexes[i] & 6);
	}
}

void GET_PILLAR_INDEXES(vector<int> &indexes, int axis, int which) {
	indexes.setsize(0);
	if(axis == 0) {
		// left to right
		// first bit Y, second bit Z
		const int y = (which >> 1) & 1;
		const int z = which & 1;
		indexes.add((y << 1) | z);
		indexes.add((1 << 2) | (y << 1) | z);
	} else if(axis == 1) {
		// back to front
		// first bit X, second bit Z
		const int x = (which >> 1) & 1;
		const int z = which & 1;
		indexes.add((x << 2) | z);
		indexes.add((x << 2) | (1 << 1) | z);
	} else { // axis == 2
		// bottom to top
		// first bit X, second bit Y
		const int x = (which >> 1) & 1;
		const int y = which & 1;
		indexes.add((x << 2) | (y << 1));
		indexes.add((x << 2) | (y << 1) | 1);
	}
}

bool VALID_SIZES(vector<int> sizes, vector<int> indexes, int max_size) {
	int total = 0;
	loopv(indexes) {
		const int size = sizes[indexes[i]];
		if(size <= 0) return false;
		total += size;
		if(total > max_size) return false;
	}
	return true;
}

// marking the size as zero allows us to determine that the chunk has already been exported,
// so that we don't export the same chunk twice
#define TRY_EXPORT_PLANE_AND_MARK_DONE(AXIS, WHICH) {\
	if(can_export_plane(sz_pack, sz_unpack, AXIS, WHICH)) { \
		exported_chunk *exp = export_plane(AXIS, WHICH); \
		if(exp != NULL) { \
			v_mixed.add(exp); \
			GET_PLANE_INDEXES(indexes, AXIS, WHICH); \
			loopv(indexes) { \
				sz_pack[indexes[i]] = sz_unpack[indexes[i]] = 0; \
			} \
		} \
	} \
}

#define TRY_EXPORT_PILLAR_AND_MARK_DONE(AXIS, WHICH) { \
	if(can_export_pillar(sz_pack, sz_unpack, AXIS, WHICH)) { \
		exported_chunk *exp = export_pillar(AXIS, WHICH); \
		if(exp != NULL) { \
			v_mixed.add(exp); \
			GET_PILLAR_INDEXES(indexes, AXIS, WHICH); \
			loopv(indexes) { \
				sz_pack[indexes[i]] = sz_unpack[indexes[i]] = 0; \
			} \
		} \
	} \
}

struct exported_chunk {
	uchar *buf;
	selinfo sel;
	int unpackedlen, packedlen;
	exported_chunk(selinfo _sel) : unpackedlen(0), packedlen(0) {
		sel = _sel;
		buf = NULL;
	}
	exported_chunk(uchar *_buf, selinfo _sel, int _unpackedlen, int _packedlen) {
		sel = _sel;
		packedlen = packedlen;
		unpackedlen = unpackedlen;
		memcpy(buf, _buf, packedlen);
	}
	~exported_chunk() {
		if(buf) free(buf);
	}

	void encode(vector<uchar> &v) {
		int scale = 0;
		while(!((sel.grid >> scale) & 1)) scale++;
		putint(v, scale);
		loopi(3) putint(v, sel.o[i] / sel.grid);
		loopi(3) putint(v, sel.s[i]);
		putint(v, unpackedlen);
		putint(v, packedlen);
		v.put(buf, packedlen);
	}
};

// not recursive
exported_chunk *exportcube_single(selinfo sel) {
//	conoutf("CHUNK (%d, %d, %d) @ %d", x, y, z, gridscale);
	if(sel.grid <= pow(2, 1)) return NULL;

	editinfo *ei = NULL;
	mpcopy(ei, sel, false);
	exported_chunk *c = new exported_chunk(sel);
//	conoutf(">> trying to pack");
	bool success = true;
	if(!packeditinfo(ei, c->unpackedlen, c->buf, c->packedlen)) {
		success = false;
	}
	if(ei) free(ei);
	return success ? c : NULL;
}

struct chunk2 {
	int x, y, z;
	int scale;
	chunk2(int _x, int _y, int _z, int _scale) {
		x = _x; y = _y; z = _z;
		scale = _scale;
	}
	chunk2(selinfo sel) {
		x = sel.o.x; y = sel.o.y; z = sel.o.z;
		scale = 0;
		while(!((sel.grid >> scale) & 1)) scale++;
	}

	selinfo get_cube_selection() {
		selinfo sel;
		sel.grid = pow(2, scale);
		sel.o = ivec(x, y, z);
		sel.s = ivec(1, 1, 1);
		return sel;
	}

	void get_subchunks(vector<chunk2> &v) {
		const selinfo sel = get_cube_selection();
		loopi(8) {
			selinfo sub_sel = sel;
			SELECTION_SET_SUBCHUNK(sub_sel, i);
			v.add(chunk2(sub_sel));
		}
	}
	chunk2 get_subchunk(int i) {
		selinfo sel = get_cube_selection();
		SELECTION_SET_SUBCHUNK(sel, i);
		return chunk2(sel);
	}
	void get_subchunk_sizes(vector<int> &packed, vector<int> &unpacked) {
		vector<chunk2> subchunks;
		get_subchunks(subchunks);
		loopv(subchunks) {
			const exported_chunk *exp = subchunks[i].export_single();
			if(!exp) {
				packed.add(-1);
				unpacked.add(-1);
			} else {
				packed.add(exp->packedlen);
				unpacked.add(exp->unpackedlen);
			}
		}
	}

	bool can_export_plane(vector<int> packed_lens, vector<int> unpacked_lens, int axis, int which) {
		vector<int> indexes;
		GET_PLANE_INDEXES(indexes, axis, which);
		vector<int> compress_bounds;
		loopv(unpacked_lens) compress_bounds.add(compressBound(unpacked_lens[i]));
		if(!VALID_SIZES(packed_lens, indexes, MAXSIZE_COMPRESSED) || !VALID_SIZES(compress_bounds, indexes, MAXSIZE_COMPRESSBOUND)) {
			return false;
		}
		return true;
	}

	bool can_export_pillar(vector<int> packed_lens, vector<int> unpacked_lens, int axis, int which) {
		vector<int> indexes;
		GET_PILLAR_INDEXES(indexes, axis, which);
		vector<int> compress_bounds;
		loopv(unpacked_lens) compress_bounds.add(compressBound(unpacked_lens[i]));
		if(!VALID_SIZES(packed_lens, indexes, MAXSIZE_COMPRESSED) || !VALID_SIZES(compress_bounds, indexes, MAXSIZE_COMPRESSBOUND)) {
			return false;
		}
		return true;
	}

	exported_chunk *export_single() {
		const selinfo sel = get_cube_selection();
		return exportcube_single(sel);
	}

	exported_chunk *export_plane(int axis, int which) {
		selinfo sel = get_cube_selection();
		SELECTION_SET_PLANE(sel, axis, which);
		return exportcube_single(sel);
	}

	exported_chunk *export_pillar(int axis, int which) {
		selinfo sel = get_cube_selection();
		SELECTION_SET_PILLAR(sel, axis, which);
		return exportcube_single(sel);
	}

	bool export_full(vector<exported_chunk *> &v) {
		// give up if the size is too small
		if(scale <= 5) {
			return false;
		}

		// try exporting full chunk
		exported_chunk *exp = export_single();
		if(exp != NULL) {
			v.add(exp);
			conoutf("Exported chunk (direct) [%d %d %d %d]", exp->sel.grid, exp->sel.o.x, exp->sel.o.y, exp->sel.o.z);
			return true;
		}

		// that didn't work, get subchunk sizes and try planes
		vector<int> sz_pack;
		vector<int> sz_unpack;
		get_subchunk_sizes(sz_pack, sz_unpack);
		exported_chunk *x1 = NULL, *x2 = NULL, *y1 = NULL, *y2 = NULL, *z1 = NULL, *z2 = NULL;
		int sz_plane_x = 0, sz_plane_y = 0, sz_plane_z = 0;

		// try LEFT-RIGHT
		if(
			   can_export_plane(sz_pack, sz_unpack, 0, 0) // left
			&& can_export_plane(sz_pack, sz_unpack, 0, 1) // right
		) {
			// export both LEFT and RIGHT
			x1 = export_plane(2, 0);
			x2 = export_plane(2, 1);
			if(x1 && x2) {
				sz_plane_x = x1->packedlen + x2->packedlen;
			}
		}
		// try BACK-FRONT
		if(
			   can_export_plane(sz_pack, sz_unpack, 1, 0) // back
			&& can_export_plane(sz_pack, sz_unpack, 1, 1) // front
		) {
			// export both BACK and FRONT
			y1 = export_plane(2, 0); // bottom
			y2 = export_plane(2, 1); // top
			if(y1 && y2) {
				sz_plane_y = y1->packedlen + y2->packedlen;
			}
		}
		// try BOTTOM-TOP
		if(
			   can_export_plane(sz_pack, sz_unpack, 2, 0) // bottom
			&& can_export_plane(sz_pack, sz_unpack, 2, 1) // top
		) {
			// export both BOTTOM and TOP
			z1 = export_plane(2, 0);
			z2 = export_plane(2, 1);
			if(z1 && z2) {
				sz_plane_z = z1->packedlen + z2->packedlen;
			}
		}

		if(sz_plane_x && (!sz_plane_y || sz_plane_x < sz_plane_y) && (!sz_plane_z || sz_plane_x < sz_plane_z)) {
			v.add(x1); v.add(x2);
			return true;
		}
		if(sz_plane_y && (!sz_plane_x || sz_plane_y < sz_plane_x) && (!sz_plane_z || sz_plane_y < sz_plane_z)) {
			v.add(y1); v.add(y2);
			return true;
		}
		if(sz_plane_z) {
			v.add(z1); v.add(z2);
			return true;
		}

		// planes didn't work, but maybe pillars will
		exported_chunk *px1 = NULL, *px2 = NULL, *px3 = NULL, *px4 = NULL;
		exported_chunk *py1 = NULL, *py2 = NULL, *py3 = NULL, *py4 = NULL;
		exported_chunk *pz1 = NULL, *pz2 = NULL, *pz3 = NULL, *pz4 = NULL;
		exported_chunk *pb1 = NULL, *pb2 = NULL, *pb3 = NULL, *pb4 = NULL; // best
		int sz_pillar_x = 0, sz_pillar_y = 0, sz_pillar_z = 0, sz_pillar_best = 0;

		// try X
		if(can_export_pillar(sz_pack, sz_unpack, 0, 0) && can_export_pillar(sz_pack, sz_unpack, 0, 1) && can_export_pillar(sz_pack, sz_unpack, 0, 2) && can_export_pillar(sz_pack, sz_unpack, 0, 3)) {
			pb1 = px1 = export_pillar(0, 0);
			pb2 = px2 = export_pillar(0, 1);
			pb3 = px3 = export_pillar(0, 2);
			pb4 = px4 = export_pillar(0, 3);
			if(px1 && px2 && px3 && px4) {
				sz_pillar_best = sz_pillar_x = px1->packedlen + px2->packedlen + px3->packedlen + px4->packedlen;
			}
		}
		// try Y
		if(can_export_pillar(sz_pack, sz_unpack, 1, 0) && can_export_pillar(sz_pack, sz_unpack, 1, 1) && can_export_pillar(sz_pack, sz_unpack, 1, 2) && can_export_pillar(sz_pack, sz_unpack, 1, 3)) {
			py1 = export_pillar(1, 0);
			py2 = export_pillar(1, 1);
			py3 = export_pillar(1, 2);
			py4 = export_pillar(1, 3);
			if(py1 && py2 && py3 && py4) {
				sz_pillar_y = py1->packedlen + py2->packedlen + py3->packedlen + py4->packedlen;
				if(sz_pillar_y < sz_pillar_best) {
					pb1 = py1; pb2 = py2; pb3 = py3; pb4 = py4;
					sz_pillar_best = sz_pillar_y;
				}
			}
		}
		// try Z
		if(can_export_pillar(sz_pack, sz_unpack, 1, 0) && can_export_pillar(sz_pack, sz_unpack, 1, 1) && can_export_pillar(sz_pack, sz_unpack, 1, 2) && can_export_pillar(sz_pack, sz_unpack, 1, 3)) {
			pz1 = export_pillar(2, 0);
			pz2 = export_pillar(2, 1);
			pz3 = export_pillar(2, 2);
			pz4 = export_pillar(2, 3);
			if(pz1 && pz2 && pz3 && pz4) {
				sz_pillar_z = pz1->packedlen + pz2->packedlen + pz3->packedlen + pz4->packedlen;
				if(sz_pillar_z < sz_pillar_best) {
					pb1 = pz1; pb2 = pz2; pb3 = pz3; pb4 = pz4;
					sz_pillar_best = sz_pillar_z;
				}
			}
		}

		vector<exported_chunk *> v_only_pillars;
		vector<exported_chunk *> v_mixed;
		if(sz_pillar_best) {
			v_only_pillars.add(pb1);
			v_only_pillars.add(pb2);
			v_only_pillars.add(pb3);
			v_only_pillars.add(pb4);
		}

		// we were unable to export both planes, but maybe we can export at least one:
		// try all planes separately
		vector<int> indexes; // buffer used by the macro
		TRY_EXPORT_PLANE_AND_MARK_DONE(2, 0); // BOTTOM
		TRY_EXPORT_PLANE_AND_MARK_DONE(2, 1); // TOP
		TRY_EXPORT_PLANE_AND_MARK_DONE(0, 0); // LEFT
		TRY_EXPORT_PLANE_AND_MARK_DONE(0, 1); // RIGHT
		TRY_EXPORT_PLANE_AND_MARK_DONE(1, 0); // BACK
		TRY_EXPORT_PLANE_AND_MARK_DONE(1, 1); // FRONT

		// try all possible pillars
		loopi(3) loopj(4) TRY_EXPORT_PILLAR_AND_MARK_DONE(i, j);

		// check for unexported chunks
		bool unexported_chunks = false;
		loopv(sz_pack) {
			if(sz_pack[i] != 0) {
				unexported_chunks = true;
				break;
			}
		}
		int sz_mixed = 0;
		loopv(v_mixed) sz_mixed += v_mixed[i]->packedlen;
		// if the pillars-only configuration is smaller then go with that one
		if(sz_pillar_best && (sz_pillar_best < sz_mixed || unexported_chunks)) {
			loopv(v_only_pillars) v.add(v_only_pillars[i]);
			return true;
		}

		// pillars-only wasn't the best configuration so go with the mixed one
		loopv(v_mixed) v.add(v_mixed[i]);

		// if there are still any subchunks with size != 0,
		// that means they weren't exported yet,
		// so do that now
		loopv(sz_pack) {
			if(sz_pack[i] != 0) {
				chunk2 subchunk = get_subchunk(i);
				if(!subchunk.export_full(v)) {
					return false;
				}
			}
		}

		return true;
	}
};

void exportmaptofile_v2(char *fn, int numents, vector<uchar> entbuf, int numvars, vector<exported_chunk *> chunks) {
	stream *of = openrawfile(fn, "wb");

	vector<uchar> buf;
	sendstring(CHUNKED_MAGIC, buf);
	putint(buf, CHUNKED_VERSION);

	// header
	putint(buf, worldscale);
	putint(buf, numents);
	putint(buf, numvars);
	putint(buf, chunks.length());

	// ents
	buf.put(entbuf.getbuf(), entbuf.length());

	// vars
	enumerate(idents, ident, id,
    {
        if((id.type!=ID_VAR && id.type!=ID_FVAR && id.type!=ID_SVAR) || !(id.flags&IDF_OVERRIDE) || id.flags&IDF_READONLY || !(id.flags&IDF_OVERRIDDEN)) continue;
		putint(buf, id.type == ID_VAR ? 0 : id.type == ID_FVAR ? 1 : 2);
		sendstring(id.name, buf);
		switch(id.type) {
			case ID_VAR: {
				putint(buf, *id.storage.i);
				break;
			}
			case ID_FVAR: {
				putfloat(buf, *id.storage.f);
				break;
			}
			default: {
				sendstring(*id.storage.s, buf);
			}
		}

    });

	loopv(chunks) {
		exported_chunk *c = chunks[i];
		c->encode(buf);
	}

	of->write(buf.getbuf(), buf.length());
	of->close();
}

void exportmap_v2(char *fn) {
	if(!fn || !fn[0]) {
		conoutf("no file specified");
		return;
	}
	vector<uchar> entbuf;
	int numents = exportentities(entbuf);

	int numvars = 0;
	enumerate(idents, ident, id,
    {
        if((id.type == ID_VAR || id.type == ID_FVAR || id.type == ID_SVAR) && id.flags&IDF_OVERRIDE && !(id.flags&IDF_READONLY) && id.flags&IDF_OVERRIDDEN) numvars++;
    });

	vector<exported_chunk *> chunks;
	int gridscale = worldscale - 1;
	int gridsize = pow(2, gridscale);
	for(int x = 0; x <= 1; x++) for(int y = 0; y <= 1; y++) for(int z = 0; z <= 1; z++) {
		vector<exported_chunk *> expdata;
		chunk2 chunk(x * gridsize, y * gridsize, z * gridsize, gridscale);
		if(!chunk.export_full(expdata)) {
			conoutf("failed to export map data");
			return;
		}
		loopv(expdata) {
			chunks.add(expdata[i]);
		}
	}
	conoutf("map exported into %d chunk%s, %d ent%s, %d var%s",
		chunks.length(), chunks.length() != 1 ? "s" : "",
		numents, numents != 1 ? "s" : "",
		numvars, numvars != 1 ? "s" : ""
	);

	exportmaptofile_v2(fn, numents, entbuf, numvars, chunks);
}
COMMAND(exportmap_v2, "s");
