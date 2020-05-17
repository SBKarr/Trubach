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
#include "SPugCache.h"
#include "SPugContext.h"
#include "ContinueToken.h"

NS_SA_EXT_BEGIN(trubach)

class TgBotHandler : public DataHandler {
public:
	TgBotHandler();

	virtual ~TgBotHandler() { }

	virtual bool isRequestPermitted(Request &) override;
	virtual bool processDataHandler(Request &req, mem::Value &result, mem::Value &input);

protected:
	void makeDigest(int64_t chatId, StringView) const;

	db::Transaction _transaction = nullptr;
	TrubachComponent *_component = nullptr;
};

void sendTgMessage(const data::Value &note, const StringView &data, NotificationFormat fmt, const data::Value &extra) {
	data::Value patch;
	if (note.isInteger()) {
		patch = data::Value({
			pair("chat_id", data::Value(note.getInteger())),
			pair("text", data::Value(data))
		});
	} else {
		if (note.isInteger("id")) {
			patch = data::Value({
				pair("chat_id", data::Value(note.getInteger("id"))),
				pair("text", data::Value(data))
			});
		} else {
			patch = data::Value({
				pair("chat_id", data::Value(note.getString("channel"))),
				pair("text", data::Value(data))
			});
		}
	}

	if (extra) {
		for (auto &it : extra.asDict()) {
			patch.setValue(move(it.second), it.first);
		}
	}

	network::Handle h(network::Handle::Method::Post, toString("https://api.telegram.org/bot", TG_BOT, "/sendMessage"));

	switch (fmt) {
	case NotificationFormat::Markdown: patch.setString("Markdown", "parse_mode"); break;
	case NotificationFormat::Html: patch.setString("HTML", "parse_mode"); break;
	default: break;
	}

	data::Stream stream;
	if constexpr (TG_PROXY) {
		h.setProxy(TG_PROXY_ADDRESS, TG_PROXY_AUTH);
	}

	h.setSendData(move(patch), data::EncodeFormat::Json);
	h.setReceiveCallback([&] (char *data, size_t size) -> size_t {
		stream.write(data, size);
		return size;
	});

	if (!h.performDataQuery()) {
		std::cout << data::EncodeFormat::Pretty << patch << "\n" << data << "\n";
	}
}

void updateTgIntegration() {
	network::Handle h(network::Handle::Method::Get, toString("https://api.telegram.org/bot", TG_BOT, "/setWebhook?url=",
			HTTP_ADDRESS, "/tg/", TG_BOT));
	if constexpr (TG_PROXY) {
		h.setProxy(TG_PROXY_ADDRESS, TG_PROXY_AUTH);
	}
	StringStream stream;
	h.setReceiveCallback([&] (char *data, size_t size) -> size_t {
		stream.write(data, size);
		return size;
	});

	h.perform();

	std::cout << stream.str() << "\n";
}

TgBotHandler::TgBotHandler() {
	_allow = AllowMethod::Post;
	_maxVarSize = 10_KiB;
	_maxRequestSize = 2_MiB;
}

bool TgBotHandler::isRequestPermitted(Request &req) {
	_transaction = db::Transaction::acquire(req.storage());
	_component = req.server().getComponent<TrubachComponent>();
	return _transaction && _component;
}

bool TgBotHandler::processDataHandler(Request &req, mem::Value &result, mem::Value &input) {
	auto &msg = input.getValue("message");
	auto text = StringView(msg.getString("text"));
	auto &chat = msg.getValue("chat");
	auto chatId = chat.getInteger("id");

	text.skipChars<StringView::CharGroup<CharGroupId::WhiteSpace>>();

	_transaction.performAsSystem([&] () {
		if (text == "/start") {
			StringStream body;
			body << "Привет!\n\n";

			sendTgMessage(data::Value(chatId), body.weak(), NotificationFormat::Html);
		} else if (text == "/sub" || text == "sub" || text == "subscribe") {
			StringStream body;
			if (auto n = _component->getNotifiers().select(_transaction, db::Query().select("id", data::Value(chatId))).getValue(0)) {
				body << "Вы уже подписаны на уведомления";
			} else {
				if (_component->getNotifiers().create(_transaction, data::Value({
					pair("id", data::Value(chatId)),
					pair("firstName", data::Value(chat.getString("first_name"))),
					pair("lastName", data::Value(chat.getString("last_name"))),
					pair("username", data::Value(chat.getString("username"))),
				}))) {
					body << "Теперь вы подписаны на уведомления";
				}
			}

			sendTgMessage(data::Value(chatId), body.weak(), NotificationFormat::Plain);
		} else if (text == "/unsub" || text == "unsub" || text == "unsubscribe") {
			StringStream body;
			if (auto n = _component->getNotifiers().select(_transaction, db::Query().select("id", data::Value(chatId))).getValue(0)) {
				_component->getNotifiers().remove(_transaction, n.getInteger("__oid"));
				body << "Вы отписаны от уведомлений";
			} else {
				body << "Вы не подписаны на уведомления";
			}
			sendTgMessage(data::Value(chatId), body.weak(), NotificationFormat::Plain);
		} else if (text == "/check" || text == "check") {
			StringStream body;
			if (auto n = _component->getNotifiers().select(_transaction, db::Query().select("id", data::Value(chatId))).getValue(0)) {
				body << "Вы подписаны на уведомления";
			} else {
				body << "Вы не подписаны на уведомления";
			}
			sendTgMessage(data::Value(chatId), body.weak(), NotificationFormat::Plain);
		} else if (text.starts_with("digest ")) {
			makeDigest(chatId, text.sub("digest "_len));
		}
		return true;
	});
	return true;
}

void TgBotHandler::makeDigest(int64_t chatId, StringView tag) const {
	Vector<data::Value *> smallChans;
	Vector<data::Value *> largeChans;

	Map<int64_t, data::Value> groups;
	Map<int64_t, data::Value> chans;
	tag.split<StringView::CharGroup<CharGroupId::WhiteSpace>>([&] (StringView w) {
		if (auto g = _component->getGroups().select(_transaction, db::Query().select("name", data::Value(w))).getValue(0)) {
			if (auto channels = _component->getChannels().select(_transaction, db::Query().select("group", data::Value(g.getInteger("__oid"))))) {
				for (auto &it : channels.asArray()) {
					auto v = _component->getVideos().select(_transaction, db::Query().select("channel", data::Value(it.getInteger("__oid")))
							.select("ctime", db::Comparation::GreatherThen, data::Value((Time::now() - TimeInterval::seconds(60 * 60 * 24)).toMicros())));
					if (v.size() > 0) {
						it.setValue(move(v), "videos");
						auto iit = chans.emplace(it.getInteger("__oid"), move(it)).first;
						if (auto v = iit->second.getInteger("subs")) {
							if (v < 10000) {
								smallChans.emplace_back(&iit->second);
							} else {
								largeChans.emplace_back(&iit->second);
							}
						}
					}
				}
			}
			groups.emplace(g.getInteger("__oid"), move(g));
		}
	});

	StringStream str;
	if (groups.size() == 1) {
		str << "<b>Новые видео на каналах группы «" << groups.begin()->second.getString("title") << "»</b>\n\n";
	} else if (!groups.empty()) {
		str << "<b>Новые видео на каналах групп ";
		auto it = groups.begin();
		str << "«" << it->second.getString("title") << "»";
		++ it;
		while (it != groups.end()) {
			auto next = it;
			++ next;

			if (next == groups.end()) {
				str << " и «" << it->second.getString("title") << "»";
			} else {
				str << ", «" << it->second.getString("title") << "»";
			}

			it = next;
		}
		str << "</b>\n\n";
	}

	if (!smallChans.empty()) {
		str << "\n<b>Развивающиеся каналы</b>:\n";
		for (auto &it : smallChans) {
			str << "\n• <u>" << it->getString("title") << "</u> <a href=\"https://www.youtube.com/channel/"
					<< it->getString("id") << "\">[ссылка]</a>\n";
			for (auto &vIt : it->getArray("videos")) {
				str << "  — <a href=\"https://www.youtube.com/watch?v="
						<< vIt.getString("id") << "\">" << vIt.getString("title") << "</a>";
				if (vIt.hasValue("duration")) {
					str << " [" << vIt.getString("duration") << "]";
				}
				str << "\n";
			}
		}
	}

	if (!largeChans.empty()) {
		if (!smallChans.empty()) {
			str << "\n\n<b>Крупные каналы</b>:\n";
		}
		for (auto &it : largeChans) {
			str << "\n• <u>" << it->getString("title") << "</u> <a href=\"https://www.youtube.com/channel/"
					<< it->getString("id") << "\">[ссылка]</a>\n";
			for (auto &vIt : it->getArray("videos")) {
				str << "  — <a href=\"https://www.youtube.com/watch?v="
						<< vIt.getString("id") << "\">" << vIt.getString("title") << "</a>";
				if (vIt.hasValue("duration")) {
					str << " [" << vIt.getString("duration") << "]";
				}
				str << "\n";
			}
		}
	}

	sendTgMessage(data::Value(chatId), str.weak(), NotificationFormat::Html, data::Value({
		pair("disable_web_page_preview", data::Value(true))
	}));
}

NS_SA_EXT_END(trubach)
