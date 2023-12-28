#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>

std::string readToken() {
  std::ifstream tokenFile(
      "/var/run/secrets/kubernetes.io/serviceaccount/token");
  std::string token;
  std::getline(tokenFile, token);
  return token;
}

std::string getPodSpec(const std::string &token, const std::string &podName) {
  CURL *curl;
  CURLcode res;
  std::string readBuffer;

  curl = curl_easy_init();
  if (curl) {
    struct curl_slist *chunk = nullptr;
    chunk =
        curl_slist_append(chunk, ("Authorization: Bearer " + token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(
        curl, CURLOPT_URL,
        "https://kubernetes.default.svc/api/v1/namespaces/default/pods/" +
            podName);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      return "Error in curl request";
    }
  }
  return readBuffer;
}

int main() {
  std::string token = readToken();
  std::string podSpec = getPodSpec(token);

  auto json = nlohmann::json::parse(podSpec);
  std::cout << json["status"]["containerStatuses"][0]["containerID"]
            << std::endl;
  // get the annotations
  std::cout << json["metadata"]["annotations"]["task_type"] << std::endl;

  return 0;
}
