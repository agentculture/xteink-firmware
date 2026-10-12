#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "xteink/ProvisioningProtocol.h"

using namespace xteink::prov;

namespace {
std::string b64(const std::string& in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    unsigned v =
        (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
    o += t[v >> 18 & 63];
    o += t[v >> 12 & 63];
    o += t[v >> 6 & 63];
    o += t[v & 63];
  }
  if (i + 1 == in.size()) {
    unsigned v = static_cast<uint8_t>(in[i]) << 16;
    o += t[v >> 18 & 63];
    o += t[v >> 12 & 63];
    o += "==";
  } else if (i + 2 == in.size()) {
    unsigned v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    o += t[v >> 18 & 63];
    o += t[v >> 12 & 63];
    o += t[v >> 6 & 63];
    o += '=';
  }
  return o;
}

Error parse(const std::string& line, Request& req) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  return parseLine(buf.data(), line.size(), req);
}

Error parseJson(const std::string& json, Request& req, int version = 1) {
  return parse(std::string("XTEINK-PROV ") + std::to_string(version) + " " + b64(json), req);
}

const char* kKey = "xtd_0a1b2c3d_SECRETsecret-_123";
}  // namespace

TEST(Provisioning, FullMessage) {
  Request r;
  const std::string json = std::string(R"({"networks":[{"ssid":"home","password":"pw1"},{"ssid":"open"}],)") +
                           R"("lan_url":"http://192.168.1.10:8080","tunnel_url":"https://x.example.com",)" +
                           R"("device_key":")" + kKey + R"(","replace_networks":true})";
  ASSERT_EQ(parseJson(json, r), Error::None);
  EXPECT_TRUE(r.replaceNetworks);
  ASSERT_EQ(r.networks.size(), 2u);
  EXPECT_EQ(r.networks[0].ssid, "home");
  EXPECT_EQ(r.networks[0].password, "pw1");
  EXPECT_EQ(r.networks[1].password, "");
  EXPECT_EQ(*r.lanUrl, "http://192.168.1.10:8080");
  EXPECT_EQ(*r.tunnelUrl, "https://x.example.com");
  EXPECT_EQ(*r.deviceKey, kKey);
  EXPECT_EQ(keyIdOf(*r.deviceKey), "0a1b2c3d");
}

TEST(Provisioning, EmptyObjectIsQuery) {
  Request r;
  EXPECT_EQ(parseJson("{}", r), Error::None);
  EXPECT_FALSE(r.deviceKey.has_value());
  EXPECT_TRUE(r.networks.empty());
}

TEST(Provisioning, CrlfTolerated) {
  Request r;
  EXPECT_EQ(parse("XTEINK-PROV 1 " + b64("{}") + "\r", r), Error::None);
}

TEST(Provisioning, EmptyStringClears) {
  Request r;
  ASSERT_EQ(parseJson(R"({"lan_url":"","device_key":""})", r), Error::None);
  ASSERT_TRUE(r.lanUrl.has_value());
  EXPECT_EQ(*r.lanUrl, "");
  EXPECT_EQ(*r.deviceKey, "");
}

TEST(Provisioning, FormatErrors) {
  Request r;
  EXPECT_EQ(parse("hello", r), Error::BadFormat);
  EXPECT_EQ(parse("XTEINK-PROV", r), Error::BadFormat);
  EXPECT_EQ(parse("XTEINK-PROV 1", r), Error::BadFormat);
  EXPECT_EQ(parse("XTEINK-PROV 1 ", r), Error::BadFormat);
  EXPECT_EQ(parse("XTEINK-PROV x " + b64("{}"), r), Error::BadFormat);
  EXPECT_EQ(parse("XTEINK-PROV 12345 " + b64("{}"), r), Error::BadFormat);
  EXPECT_EQ(parseJson("{}", r, 2), Error::UnsupportedVersion);
  EXPECT_EQ(parseJson("{}", r, 0), Error::UnsupportedVersion);
}

TEST(Provisioning, Base64Errors) {
  Request r;
  EXPECT_EQ(parse("XTEINK-PROV 1 e30", r), Error::BadBase64);       // missing padding
  EXPECT_EQ(parse("XTEINK-PROV 1 e3!=", r), Error::BadBase64);      // bad char
  EXPECT_EQ(parse("XTEINK-PROV 1 e=30", r), Error::BadBase64);      // pad in middle
  EXPECT_EQ(parse("XTEINK-PROV 1 e30=e30=", r), Error::BadBase64);  // pad before end
}

TEST(Provisioning, JsonErrors) {
  Request r;
  EXPECT_EQ(parseJson("not json", r), Error::BadJson);
  EXPECT_EQ(parseJson("[1,2]", r), Error::BadJson);
  EXPECT_EQ(parseJson(R"({"networks":)", r), Error::BadJson);
  EXPECT_EQ(parseJson(R"({"a":{"b":{"c":{"d":{"e":{}}}}}})", r), Error::BadJson);  // nesting
}

TEST(Provisioning, FieldValidation) {
  Request r;
  EXPECT_EQ(parseJson(R"({"networks":"x"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[1]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"password":"p"}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":""}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":"a\u0000b"}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":5}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":"a"},{"ssid":"a"}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"replace_networks":"yes"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"lan_url":"ftp://x"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"lan_url":"http://a b"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"tunnel_url":"http://insecure.example.com"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"device_key":"nope"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"device_key":"xtd_ZZZZZZZZ_secret"})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"device_key":"xtd_0a1b2c3d_"})", r), Error::InvalidField);
}

TEST(Provisioning, Limits) {
  Request r;
  const std::string ssid32(32, 'a'), ssid33(33, 'a'), pw64(64, 'p'), pw65(65, 'p');
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":")" + ssid32 + R"(","password":")" + pw64 + R"("}]})", r), Error::None);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":")" + ssid33 + R"("}]})", r), Error::InvalidField);
  EXPECT_EQ(parseJson(R"({"networks":[{"ssid":"a","password":")" + pw65 + R"("}]})", r), Error::InvalidField);

  std::string nine = R"({"networks":[)";
  for (int i = 0; i < 9; i++) nine += std::string(i ? "," : "") + R"({"ssid":"n)" + std::to_string(i) + R"("})";
  nine += "]}";
  EXPECT_EQ(parseJson(nine, r), Error::TooManyNetworks);
  std::string eight = nine;
  eight.erase(eight.find(R"(,{"ssid":"n8"})"), 14);
  EXPECT_EQ(parseJson(eight, r), Error::None);
  EXPECT_EQ(r.networks.size(), 8u);

  EXPECT_EQ(parseJson(R"({"lan_url":"http://)" + std::string(200, 'a') + R"("})", r), Error::InvalidField);
}

TEST(Provisioning, OversizeLineAndJson) {
  Request r;
  std::string huge(kMaxLineLen + 1, 'A');
  EXPECT_EQ(parse("XTEINK-PROV 1 " + huge, r), Error::TooLong);
  // Valid base64 whose decoded JSON exceeds kMaxJsonLen but fits the line cap.
  const std::string big = R"({"x":")" + std::string(kMaxJsonLen, 'a') + R"("})";
  ASSERT_LE(14 + b64(big).size(), kMaxLineLen);
  EXPECT_EQ(parseJson(big, r), Error::TooLong);
}

TEST(Provisioning, MalformedNeverCrashes) {
  Request r;
  for (int n = 0; n < 300; n++) {
    std::string junk = "XTEINK-PROV 1 ";
    for (int i = 0; i < n; i++) junk += static_cast<char>((i * 31 + n * 7) % 256);
    parse(junk, r);
  }
  SUCCEED();
}

TEST(Provisioning, ReplyLines) {
  EXPECT_EQ(buildErrLine(Error::NotInProvisioningMode), "XTEINK-PROV-ERR 1 not_in_provisioning_mode\n");
  const std::string ack = buildAckLine("AA:BB:CC:DD:EE:FF", "1.2.3", 3, "0a1b2c3d");
  EXPECT_EQ(ack.rfind("XTEINK-PROV-ACK 1 ", 0), 0u);
  EXPECT_EQ(ack.back(), '\n');
  // Round-trip the payload with our own decoder path.
  std::string payload = ack.substr(18, ack.size() - 19);
  const std::string asRequest = "XTEINK-PROV 1 " + payload;
  std::vector<char> buf(asRequest.begin(), asRequest.end());
  Request r;
  EXPECT_EQ(parseLine(buf.data(), buf.size(), r), Error::None);  // valid JSON object, unknown fields ignored
  EXPECT_LT(ack.size(), 256u);                                   // fits HWCDC's 256 B TX path in one write
}

TEST(Provisioning, AckNeverContainsSecrets) {
  const std::string ack = buildAckLine("AA:BB:CC:DD:EE:FF", "1.2.3", 1, keyIdOf(kKey));
  EXPECT_EQ(ack.find("SECRET"), std::string::npos);
  EXPECT_EQ(ack.find(kKey), std::string::npos);
}

TEST(Provisioning, IsProvisioningLine) {
  EXPECT_TRUE(isProvisioningLine("XTEINK-PROV 1 x", 15));
  EXPECT_FALSE(isProvisioningLine("XTEINK-PROV-ACK 1 x", 19));
  EXPECT_FALSE(isProvisioningLine("CMD:SCREENSHOT", 14));
}
