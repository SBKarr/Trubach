/*
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004

Copyright (C) 2020 SBKarr <sbkarr@stappler.org>

Everyone is permitted to copy and distribute verbatim or modified
copies of this license document, and changing it is allowed as long
as the name is changed.

           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.
*/

#include "Admin.h"

NS_SA_EXT_BEGIN(trubach)

static data::Value Admin_getChannelPatch(StringView id, int64_t section, bool username = false) {
	StringStream apiUrl;
	apiUrl << "https://www.googleapis.com/youtube/v3/channels?part=snippet%2CcontentDetails%2Cstatistics&key=" << YOUTUBE_API_KEY;
	if (username) {
		apiUrl << "&forUsername=" << id;
	} else {
		apiUrl << "&id=" << id;
	}

	network::Handle h(NetworkHandle::Method::Get, apiUrl.str());
	if (auto val = h.performDataQuery()) {
		auto etag = val.getString("etag");
		if (auto &item = val.getValue("items").getValue(0)) {
			auto &snippet = item.getValue("snippet");
			auto &statistics = item.getValue("statistics");
			auto &thumbs = snippet.getValue("thumbnails");
			auto &details = item.getValue("contentDetails");

			auto uploads = details.getValue("relatedPlaylists").getString("uploads");

			data::Value patch({
				pair("created", data::Value(Time::fromHttp(snippet.getString("publishedAt")).toMicros())),
				pair("id", item.getValue("id")),
				pair("uploads", data::Value(uploads)),
				pair("etag", data::Value(etag)),
				pair("title", snippet.getValue("title")),
				pair("desc", snippet.getValue("description")),
				pair("subs", statistics.getValue("subscriberCount")),
				pair("nvideos", statistics.getValue("videoCount")),
				pair("nviews", statistics.getValue("viewCount")),

				pair("thumbs", data::Value({
					pair("default", data::Value(thumbs.getValue("default").getString("url"))),
					pair("high", data::Value(thumbs.getValue("high").getString("url"))),
					pair("medium", data::Value(thumbs.getValue("medium").getString("url"))),
				}))
			});

			if (section) {
				patch.setInteger(section, "section");
			}

			return patch;
		}
	}

	return data::Value();
}

static data::Value Admin_getChannelStats(StringView id) {
	StringStream apiUrl;
	apiUrl << "https://www.googleapis.com/youtube/v3/channels?part=statistics&key=" << YOUTUBE_API_KEY << "&id=" << id;

	network::Handle h(NetworkHandle::Method::Get, apiUrl.str());
	if (auto val = h.performDataQuery()) {
		auto etag = val.getString("etag");
		if (auto &item = val.getValue("items").getValue(0)) {
			auto &statistics = item.getValue("statistics");

			data::Value patch({
				pair("subs", statistics.getValue("subscriberCount")),
				pair("nvideos", statistics.getValue("videoCount")),
				pair("nviews", statistics.getValue("viewCount")),
			});

			return patch;
		}
	}

	return data::Value();
}

static void Admin_getChannelVideos(StringView id, StringView uploads, int64_t channel, const Callback<void(const data::Value &)> &cb) {
	auto addItems = [&] (const data::Value &items) {
		for (auto &item : items.asArray()) {
			auto &snippet = item.getValue("snippet");
			auto &thumbs = snippet.getValue("thumbnails");

			cb(data::Value({
				pair("id", data::Value(snippet.getValue("resourceId").getString("videoId"))),
				pair("itemId", data::Value(item.getString("id"))),
				pair("published", data::Value(Time::fromHttp(snippet.getString("publishedAt")).toMicros())),
				pair("etag", data::Value(item.getString("etag"))),
				pair("title", data::Value(snippet.getString("title"))),
				pair("desc", data::Value(snippet.getString("description"))),
				pair("channel", data::Value(channel)),
				pair("thumbs", data::Value({
					pair("default", data::Value(thumbs.getValue("default").getString("url"))),
					pair("high", data::Value(thumbs.getValue("high").getString("url"))),
					pair("medium", data::Value(thumbs.getValue("medium").getString("url"))),
					pair("standard", data::Value(thumbs.getValue("standard").getString("url"))),
				})),
			}));
		}
	};

	auto apiUrl = toString("https://www.googleapis.com/youtube/v3/playlistItems?key=", YOUTUBE_API_KEY,
			"&playlistId=", uploads, "&part=snippet&maxResults=50");

	network::Handle h(NetworkHandle::Method::Get, apiUrl);

	if (auto val = h.performDataQuery()) {
		addItems(val.getValue("items"));

		auto tok = val.getString("nextPageToken");
		while (!tok.empty()) {
			apiUrl = toString("https://www.googleapis.com/youtube/v3/playlistItems?key=", YOUTUBE_API_KEY,
						"&playlistId=", uploads, "&part=snippet&maxResults=50&pageToken=", tok);

			network::Handle h(NetworkHandle::Method::Get, apiUrl);
			if (auto val = h.performDataQuery()) {
				addItems(val.getValue("items"));
				tok = val.getString("nextPageToken");
			} else {
				tok.clear();
			}
		}
	}
}

static void Admin_addChannel(const TrubachComponent *c, const db::Transaction &t, StringView urlStr) {
	data::Value patch;
	UrlView url(urlStr);
	if (url.path.starts_with("/channel/")) {
		auto chanId = url.path.sub("/channel/"_len).readUntil<StringView::Chars<'/'>>();
		patch = Admin_getChannelPatch(chanId, 0);
	} else if (url.path.starts_with("/user/")) {
		auto chanId = url.path.sub("/user/"_len).readUntil<StringView::Chars<'/'>>();
		patch = Admin_getChannelPatch(chanId, 0, true);
	}

	int64_t id = 0;
	if (patch) {
		id = c->getChannels().create(t, patch, db::Conflict("id", db::Query::Select(),
				Vector<String>({"etag", "title", "desc", "subs", "nvideos", "nviews", "section", "uploads"}))
			.setFlags(db::Conflict::None)).getInteger("__oid");
		if (id) {
			auto v =  c->getChannels().getProperty(t, id, "videos", {"title"});
			if (v.empty()) {
				Admin_getChannelVideos(patch.getString("id"), patch.getString("uploads"), id, [&] (const data::Value &patch) {
					c->getVideos().create(t, patch, db::Conflict("id", db::Query::Select(),
							Vector<String>({"etag", "title", "desc", "published"})).setFlags(db::Conflict::None));
				});
			} else {
				if (patch.getInteger("nvideos") < int64_t(v.size())) {
					std::cout << patch.getString("title") << " " << patch.getInteger("nvideos") << " " << v.size() << "\n";
				}
			}
		}
	}
}

class AdminAddChannelHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));
		if (!v.host.empty() && v.host != _request.getHostname()) {
			return HTTP_BAD_REQUEST;
		}

		data::Value patch;
		UrlView url(_inputFields.getString("url"));
		if (url.path.starts_with("/channel/")) {
			auto path = url.path.sub("/channel/"_len).readUntil<StringView::Chars<'/'>>();
			patch = Admin_getChannelPatch(path, _inputFields.getInteger("section"));
		} else if (url.path.starts_with("/user/")) {
			auto path = url.path.sub("/user/"_len).readUntil<StringView::Chars<'/'>>();
			patch = Admin_getChannelPatch(path, _inputFields.getInteger("section"), true);
		}

		int64_t id = 0;
		if (patch) {
			id = _component->getChannels().create(_transaction, patch, db::Conflict("id", db::Query::Select(),
					Vector<String>({"etag", "title", "desc", "subscribers", "nvideos", "nviews", "section", "uploads"}))
				.setFlags(db::Conflict::None)).getInteger("__oid");
			if (id) {
				Admin_getChannelVideos(patch.getString("id"), patch.getString("uploads"), id, [&] (const data::Value &patch) {
					_component->getVideos().create(_transaction, patch, db::Conflict("id", db::Query::Select(),
							Vector<String>({"etag", "title", "desc", "published"})).setFlags(db::Conflict::None));
				});
			}
		}

		return _request.redirectTo(v.get());
	}
};

class AdminUpdateChannelHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));

		if (auto ch = _component->getChannels().get(_transaction, _params.getInteger("id"))) {
			auto patch = Admin_getChannelPatch(ch.getString("id"), ch.getInteger("section"));

			_component->getChannels().update(_transaction, _params.getInteger("id"), patch);
		}

		return _request.redirectTo(v.get());
	}
};

class AdminReadVideoslHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));

		if (auto ch = _component->getChannels().get(_transaction, _params.getInteger("id"))) {
			auto id = ch.getString("id");
			auto uploads = ch.getString("uploads");

			Admin_getChannelVideos(id, uploads, _params.getInteger("id"), [&] (const data::Value &patch) {
				_component->getVideos().create(_transaction, patch, db::Conflict("id", db::Query::Select(),
						Vector<String>({"etag", "title", "desc", "published"})).setFlags(db::Conflict::None));
			});
		}

		return _request.redirectTo(v.get());
	}
};

class AdminUpdateStatsHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));

		auto snap = _component->getChansnap().create(_transaction, data::Value(data::Value::Type::DICTIONARY));
		auto ch = _component->getChannels().select(_transaction, db::Query());
		for (auto &it : ch.asArray()) {
			auto oid = it.getInteger("__oid");
			auto id = StringView(it.getString("id"));
			auto patch = Admin_getChannelStats(id);

			_component->getChannels().update(_transaction, it, patch);

			patch.setInteger(snap.getInteger("__oid"), "snap");
			patch.setInteger(oid, "chan");
			_component->getChanstat().create(_transaction, patch);
		}

		if (!v.path.empty()) {
			return _request.redirectTo(v.get());
		}
		output::writeData(_request, data::Value({
			pair("date", data::Value(Time::now().toMicros())),
			pair("OK", data::Value(true))
		}));
		return DONE;
	}
};

class AdminSetGroupHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));

		if (auto c = _component->getChannels().get(_transaction, _inputFields.getInteger("channel"))) {
			if (auto g = _component->getGroups().get(_transaction, _inputFields.getInteger("group"))) {
				_component->getChannels().update(_transaction, c, data::Value({
					pair("group", data::Value(g.getInteger("__oid")))
				}));
				auto v = _component->getChannels().getProperty(_transaction, c, "videos");
				for (auto &it : v.asArray()) {
					_component->getVideos().update(_transaction, it, data::Value({
						pair("group", data::Value(g.getInteger("__oid")))
					}));
				}
			}
		}

		return _request.redirectTo(v.get());
	}
};

NS_SA_EXT_END(trubach)
