/**
 * Renders LittleFS templates with JSON from GET /state and POST /mode.
 * Placeholders: {{key}} — keys match JSON field names.
 */
(function () {
  var stateTpl = null;
  var weatherTpl = null;

  function applyTemplate(tpl, data) {
    return tpl.replace(/\{\{(\w+)\}\}/g, function (_, key) {
      return data[key] != null ? data[key] : "";
    });
  }

  function loadTemplates() {
    if (stateTpl) {
      return Promise.resolve();
    }
    return Promise.all([
      fetch("/state.fragment.html").then(function (r) {
        if (!r.ok) throw new Error("state template");
        return r.text();
      }),
      fetch("/weather.fragment.html").then(function (r) {
        if (!r.ok) throw new Error("weather template");
        return r.text();
      }),
    ]).then(function (parts) {
      stateTpl = parts[0];
      weatherTpl = parts[1];
    });
  }

  function buttonClass(mode, name) {
    return mode === name ? "active" : "";
  }

  function renderState(data) {
    var weather = data.weather;
    var payload = {
      mode: data.mode || "",
      lightClass: buttonClass(data.mode, "LIGHT"),
      ambientClass: buttonClass(data.mode, "AMBIENT"),
      forecastClass: buttonClass(data.mode, "FORECAST"),
      weatherSection: "",
    };

    if (weather) {
      payload.condition = weather.condition;
      payload.temperature = weather.temperature;
      payload.wind = weather.wind;
      payload.windDeg = weather.windDeg;
      payload.windDir = weather.windDir;
      payload.rainSuffix = weather.rain ? " · " + weather.rain : "";
      payload.weatherSection = applyTemplate(weatherTpl, payload);
    }

    var html = applyTemplate(stateTpl, payload);
    var el = document.getElementById("state");

    el.outerHTML = html;

    if (typeof htmx !== "undefined") {
      htmx.process(document.getElementById("state"));
    }

    const infoBtn = document.getElementById("info-btn");
    if (infoBtn && payload.mode) {
        infoBtn.setAttribute("data-mode", payload.mode);
    }
  }

  function refreshState() {
    return loadTemplates()
      .then(function () {
        return fetch("/state");
      })
      .then(function (r) {
        if (!r.ok) throw new Error("state");
        return r.json();
      })
      .then(renderState)
      .catch(function (err) {
        console.error("[UI]", err);
        var el = document.getElementById("state");
        if (el) {
          el.innerHTML =
            '<div class="mode-name text-gradient">UI not loaded</div>';
        }
      });
  }

  window.refreshState = refreshState;

  document.body.addEventListener("htmx:afterRequest", function (evt) {
    var path = evt.detail.pathInfo && evt.detail.pathInfo.requestPath;

    if (path !== "/mode" || !evt.detail.successful) {
      return;
    }

    try {
      renderState(JSON.parse(evt.detail.xhr.responseText));
    } catch (e) {
      refreshState();
    }
  });

  document.addEventListener("DOMContentLoaded", function () {
    refreshState();
    setInterval(refreshState, 5000);

    var infoBtn = document.getElementById("info-btn");
    var modal = document.getElementById("info-modal");
    var closeBtn = document.getElementById("modal-close-btn");
    var reconfigureBtn = document.getElementById("reconfigure-btn");

    infoBtn.addEventListener("click", function () {
      modal.hidden = false;
    });
    closeBtn.addEventListener("click", function () {
      modal.hidden = true;
    });
    reconfigureBtn.addEventListener("click", function () {
      if (
        !confirm(
          "Device will switch to setup mode. Reconnect your phone/PC to the 'forecaster_wifi' network to reconfigure."
        )
      ) {
        return;
      }
      fetch("/reconfigure", { method: "POST" })
        .then(function () {
          modal.hidden = true;
        })
        .catch(function (err) {
          console.error("[UI]", err);
        });
    });
  });
})();
