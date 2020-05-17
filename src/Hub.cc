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

#include "Trubach.h"
#include "InputFilter.h"

NS_SA_EXT_BEGIN(trubach)

static data::Value HubHandler_readEntry(StringView r) {
	auto readValue = [&] (StringView &r, StringView tag) -> StringView {
		if (r.is('>')) {
			++ r;
			auto val = r.readUntil<StringView::Chars<'<'>>();
			if (r.is("</")) {
				r += 2;
				if (r.is(tag)) {
					r += tag.size();
					if (r.is(">")) {
						++ r;
						return val;
					}
				}
			}
		}
		return StringView();
	};

	data::Value d;
	r.skipUntilString("<entry>");
	if (r.is("<entry>")) {
		r += "<entry>"_len;
		r.skipUntil<StringView::Chars<'<'>>();
		while (r.is('<')) {
			++ r;
			auto tag = r.readUntil<StringView::Chars<'>'>>();
			tag.trimChars<StringView::CharGroup<CharGroupId::WhiteSpace>>();
			if (tag == "id" || tag == "yt:videoId" || tag == "yt:channelId" || tag == "title" || tag == "name" || tag == "uri") {
				auto val = readValue(r, tag);
				if (!val.empty()) {
					d.setString(val, tag);
				}
			} else if (tag.starts_with("link ")) {
				tag += "link "_len;
				tag.skipUntilString("href=\"");
				if (tag.is("href=\"")) {
					tag += "href=\""_len;
					auto link = tag.readUntil<StringView::Chars<'"'>>();
					d.setString(link, "link");
				}
			} else if (tag == "author" || tag == "/author") {
				if (r.is('>')) {
					++ r;
				}
			} else if (tag == "published" || tag == "updated") {
				auto val = readValue(r, tag);
				if (!val.empty()) {
					d.setInteger(Time::fromHttp(val).toMicros(), tag);
				}
			} else {
				break;
			}
			r.skipUntil<StringView::Chars<'<'>>();
		}
	}

	return d;
}

static data::Value HubHandler_updateVideo(StringView id, int64_t channel = 0) {
	auto apiUrl = toString("https://www.googleapis.com/youtube/v3/videos?key=", YOUTUBE_API_KEY,
			"&id=", id, "&part=snippet");

	network::Handle h(NetworkHandle::Method::Get, apiUrl);

	if (auto val = h.performDataQuery()) {
		for (auto &item : val.getArray("items")) {
			auto &snippet = item.getValue("snippet");
			auto &thumbs = snippet.getValue("thumbnails");

			data::Value ret({
				pair("id", data::Value(item.getString("id"))),
				pair("published", data::Value(Time::fromHttp(snippet.getString("publishedAt")).toMicros())),
				pair("etag", data::Value(item.getString("etag"))),
				pair("title", data::Value(snippet.getString("title"))),
				pair("desc", data::Value(snippet.getString("description"))),
				pair("thumbs", data::Value({
					pair("default", data::Value(thumbs.getValue("default").getString("url"))),
					pair("high", data::Value(thumbs.getValue("high").getString("url"))),
					pair("medium", data::Value(thumbs.getValue("medium").getString("url"))),
					pair("standard", data::Value(thumbs.getValue("standard").getString("url"))),
				})),
			});

			if (channel) {
				ret.setInteger(channel, "channel");
			}

			return ret;
		}
	}

	return data::Value();
}

class HubHandler : public DataHandler {
public:
	HubHandler();

	virtual ~HubHandler() { }

	virtual bool isRequestPermitted(Request &) override;
	virtual int onTranslateName(Request &) override;
	virtual void onFilterComplete(InputFilter *f) override;

	bool subscribe(StringView, int64_t);
	bool unsubscribe(StringView);

protected:
	data::Value pushData(const data::Value &) const;

	db::Transaction _transaction = nullptr;
	TrubachComponent *_component = nullptr;
};

HubHandler::HubHandler() {
	_allow = AllowMethod::Post | AllowMethod::Get;
	_maxVarSize = 10_KiB;
	_maxRequestSize = 2_MiB;
	_required = db::InputConfig::Require::Body | db::InputConfig::Require::Files;
}

int HubHandler::onTranslateName(Request &req) {
	if (req.getMethod() == Request::Get) {
		auto args = req.getParsedQueryArgs();
		auto challenge = args.getString("hub.challenge");
		auto topic = args.getString("hub.topic");
		auto mode = args.getString("hub.mode");

		if (StringView(topic).starts_with("http://push-pub.appspot.com")) {
			if (!challenge.empty()) {
				req << challenge;
				return DONE;
			}
			return HTTP_BAD_REQUEST;
		}

		UrlView url(topic);
		StringView query = url.query;
		if (query.empty()) {
			return HTTP_BAD_REQUEST;
		}

		bool ret = (mode == "unsubscribe");
		query.skipUntilString("channel_id=");
		if (query.is("channel_id=")) {
			query += "channel_id="_len;
			auto id = query.readUntil<StringView::Chars<'&'>>();
			if (!id.empty()) {
				if (mode == "subscribe") {
					ret = subscribe(id, args.getInteger("hub.lease_seconds"));
				} else if (mode == "unsubscribe" || mode == "denied") {
					ret = unsubscribe(id);
				}
			}
		}

		if (ret) {
			if (!challenge.empty()) {
				req << challenge;
				return DONE;
			}
		}
		return HTTP_BAD_REQUEST;
	}
	return DataHandler::onTranslateName(req);
}

bool HubHandler::isRequestPermitted(Request &req) {
	_transaction = db::Transaction::acquire(req.storage());
	_component = req.server().getComponent<TrubachComponent>();
	return _transaction && _component;
}

void HubHandler::onFilterComplete(InputFilter *f) {
	auto &b = f->getBody();

	auto d = HubHandler_readEntry(b.weak());

	_transaction.performAsSystem([&] () -> bool {
		d = pushData(d);
		if (d) {
			StringStream body;
			body << "Новое видео на канале \"" << d.getString("channel") << "\": <a href=\""
					<< d.getString("link") << "\">" << d.getString("title") << "</a>";

			auto notes = _component->getNotifiers().select(_transaction, db::Query());
			for (auto &it : notes.asArray()) {
				sendTgMessage(it, body.weak(), NotificationFormat::Html);
			}
		}
		return true;
	});

	_request.setStatus(HTTP_OK);
}

bool HubHandler::subscribe(StringView id, int64_t lease) {
	return _transaction.performAsSystem([&] () -> bool {
		if (auto c = _component->getChannels().select(_transaction, db::Query().select("id", data::Value(id))).getValue(0)) {
			auto leaseTime = Time::seconds(Time::now().toSeconds() - 1 + (lease ? lease : 432000));

			_component->getChannels().update(_transaction, c, data::Value({
				pair("subscribed", data::Value(leaseTime.toMicros()))
			}));

			_component->getMessages().create(_transaction, data::Value({
				pair("tag", data::Value("subsctiption")),
				pair("type", data::Value("subscribe")),
				pair("text", data::Value(toString("Subscribed: ", c.getString("title")))),
				pair("object", data::Value(id))
			}));

			return true;
		}
		return false;
	});
}

bool HubHandler::unsubscribe(StringView id) {
	return _transaction.performAsSystem([&] () -> bool {
		if (auto c = _component->getChannels().select(_transaction, db::Query().select("id", data::Value(id))).getValue(0)) {
			_component->getChannels().update(_transaction, c, data::Value({
				pair("subscribed", data::Value(0))
			}));

			_component->getMessages().create(_transaction, data::Value({
				pair("tag", data::Value("subsctiption")),
				pair("type", data::Value("unsubscribe")),
				pair("text", data::Value(toString("Unsubscribed: ", c.getString("title")))),
				pair("object", data::Value(id))
			}));

			return true;
		}
		return false;
	});
}

data::Value HubHandler::pushData(const data::Value &d) const {
	auto vidId = d.getString("yt:videoId");
	auto chanId = d.getString("yt:channelId");
	if (auto v = _component->getVideos().select(_transaction, db::Query().select("id", data::Value(vidId))).getValue(0)) {
		auto patch = HubHandler_updateVideo(vidId);
		_component->getVideos().update(_transaction, v, patch);
	} else {
		if (auto c = _component->getChannels().select(_transaction, db::Query().select("id", data::Value(chanId))).getValue(0)) {
			auto patch = HubHandler_updateVideo(vidId, c.getInteger("__oid"));
			_component->getVideos().create(_transaction, patch);
			return data::Value({
				pair("link", data::Value(d.getString("link"))),
				pair("title", data::Value(d.getString("title"))),
				pair("channel", data::Value(c.getString("title"))),
			});
		}
	}
	return data::Value();
}

NS_SA_EXT_END(trubach)
