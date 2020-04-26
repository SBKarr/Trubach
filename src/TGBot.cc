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
	db::Transaction _transaction = nullptr;
	TrubachComponent *_component = nullptr;
};

void sendTgMessage(const data::Value &note, const StringView &data, NotificationFormat fmt) {
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
		}
		return true;
	});
	return true;
}

NS_SA_EXT_END(trubach)
