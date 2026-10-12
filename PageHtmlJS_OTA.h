//***************************************************
// Page HTML et Javascript mise à jour du code par OTA
//***************************************************
const char *OtaHtml = R"====(
  <!doctype html>
  <html><head><meta charset="UTF-8">
  <link rel="stylesheet" href="/runtime.css">
  <link rel="stylesheet" href="/commun.css">
  <style>
    input {font-size:20px;}
    .liste{display:flex;justify-content:center;text-align:left;} 
    #onglets2{display:block;}
    .Bparametres{border:inset 10px azure;}
    .Bota{border:inset 4px azure;}
  </style>
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js"></script>
  <title>Update OTA F1ATB</title>
  </head>
  <body onload="Init();">
    <div id="lesOnglets"></div>
    <h2 >Web OTA</h2>
    <h4>Mise à jour par Wifi ou Ethernet</h4>
    <div class="liste">
      Votre version actuelle du routeur : <span id ="Version_actu"></span>
    </div>
    <br>
    <div class="liste">  
      Version(s) disponible(s) :
    </div>
    <div class="liste">
      <iframe src="https://f1atb.fr/web_tool/scan_dir_bin.php"  style="width:50% ; height:150px"></iframe>
    </div>
    <div class="liste">
      <ul>
        <li>1 - <a href="/Export" >Sauvegardez vos paramètres</a> sur le PC si la partie entière de la version change. Pas nécessaire si uniquement la partie décimale.</li>
        <li>2 - Téléchargez sur votre ordinateur, la version binaire du logiciel du routeur souhaitée <br>(Solar_Router_Vxx.xx.ino.bin) en cliquant dessus</li>
        <li>3 - Cliquez sur "Choisir un fichier" et sélectionnez ce binaire sur votre ordinateur</li>
        <li>4 - Cliquez sur "Mettre à jour"</li>
        <li>5 - <a href="/Export" >Importez si besoin vos anciens paramètres</a> du PC </li>
      </ul>
    </div>
    <form method="POST" action="#" enctype="multipart/form-data" id="upload_form">
      <input type="file" name="update" id="fichier">
      <input class="bouton" id="submit_btn" type="submit" value="Mettre à jour">
    </form>
    <div id="prg">progression: 0%</div>
    <script>
          var BordsInverse=[".Bparametres",".Bota"];
          var otaBusy = false;
          $('form').submit(function(e){
            e.preventDefault();
            if (otaBusy) return;
            var file = $('#fichier')[0].files[0];
            if (!file || !file.size) {
              $('#prg').html('Sélectionnez un fichier binaire non vide.');
              console.error('[OTA] Aucun fichier binaire non vide sélectionné');
              return;
            }
            let data = new FormData($('#upload_form')[0]);
            otaBusy = true;
            $('#fichier, #submit_btn').prop('disabled', true);
            $.ajax({
              url: '/update',
              type: 'POST',
              data: data,
              contentType: false,
              processData: false,
              xhr: function() {
                let xhr = new window.XMLHttpRequest();
                xhr.upload.addEventListener('progress', function(evt) {
                  if (evt.lengthComputable) {
                    let pct = Math.round(evt.loaded / evt.total * 100);
                    $('#prg').html(pct < 100
                      ? 'Envoi: ' + pct + '%'
                      : 'Envoi terminé, validation en cours...');
                  }
                }, false);
                return xhr;
              },
              success: function(response) {
                if (response.trim() === 'OK') {
                  $('#prg').html('✓ Mise à jour validée. Redémarrage en cours...');
                  console.log('[OTA] Mise à jour validée par le serveur. Démarrage du polling...');
                  startPolling();
                } else {
                  $('#prg').html('✗ Échec de la mise à jour: ' + response);
                  console.error('[OTA] Échec:', response);
                  otaBusy = false;
                  $('#fichier, #submit_btn').prop('disabled', false);
                }
              },
              error: function(xhr) {
                if (xhr.status === 0) {
                  // Connexion coupée: l'ESP32 a probablement redémarré avant d'envoyer la réponse
                  $('#prg').html('Connexion interrompue, vérification du redémarrage...');
                  console.warn('[OTA] Connexion interrompue (status 0) — redémarrage probable. Démarrage du polling...');
                  startPolling();
                } else {
                  $('#prg').html('✗ Erreur HTTP ' + xhr.status);
                  console.error('[OTA] Erreur HTTP:', xhr.status, xhr.statusText);
                  otaBusy = false;
                  $('#fichier, #submit_btn').prop('disabled', false);
                }
              }
            });
          });

          function startPolling() {
            var baseUrl = window.location.origin;
            var maxAttempts = 40; // 80 secondes max
            var attempts = 0;
            var dotCount = 0;
            var t0 = Date.now();
            var finished = false;
            var inFlight = false;
            var controller = null;
            var requestTimer;

            console.log('[OTA] Polling démarré —', baseUrl);

            var dotsTimer = setInterval(function() {
              dotCount = (dotCount % 5) + 1;
              $('#prg').html('Redémarrage en cours' + '.'.repeat(dotCount));
            }, 500);

            function stopPolling() {
              finished = true;
              clearInterval(pollTimer);
              clearInterval(dotsTimer);
              clearTimeout(timeoutTimer);
              clearTimeout(requestTimer);
              if (controller) controller.abort();
            }

            function pollingTimeout() {
              stopPolling();
              var elapsed = ((Date.now() - t0) / 1000).toFixed(1);
              console.error('[OTA] Timeout après ' + elapsed + 's — ESP32 non joignable');
              $('#prg').html('⚠ Délai dépassé. <a href="/">Cliquez ici pour recharger manuellement.</a>');
              otaBusy = false;
              $('#fichier, #submit_btn').prop('disabled', false);
            }

            var timeoutTimer = setTimeout(pollingTimeout, 80000);
            var pollTimer = setInterval(function() {
              if (finished || inFlight) return;
              attempts++;
              var elapsed = ((Date.now() - t0) / 1000).toFixed(1);
              if (attempts > maxAttempts) {
                pollingTimeout();
                return;
              }
              console.log('[OTA] Tentative ' + attempts + '/' + maxAttempts + ' (' + elapsed + 's)...');
              inFlight = true;
              var probe = new AbortController();
              controller = probe;
              var probeTimer = setTimeout(function() { probe.abort(); }, 3000);
              requestTimer = probeTimer;
              fetch(baseUrl + '/', { cache: 'no-store', signal: probe.signal })
                .then(function(resp) {
                  if (finished) return;
                  if (resp.ok) {
                    stopPolling();
                    console.log('[OTA] ESP32 joignable après ' + elapsed + 's — rechargement');
                    $('#prg').html('✓ ESP32 redémarré ! Rechargement...');
                    clearCachesAndReload(baseUrl + '/OTA');
                  }
                })
                .catch(function(err) {
                  if (finished) return;
                  console.log('[OTA] Tentative ' + attempts + ' — pas de réponse (' + (err.message || 'network error') + ')');
                })
                .finally(function() {
                  clearTimeout(probeTimer);
                  inFlight = false;
                  if (controller === probe) controller = null;
                });
            }, 2000);
          }

          function clearCachesAndReload(url) {
            var target = url + '?v=' + Date.now();
            if ('caches' in window) {
              caches.keys().then(function(keys) {
                console.log('[OTA] ' + keys.length + ' cache(s) à effacer : ' + JSON.stringify(keys));
                return Promise.all(keys.map(function(k) {
                  return caches.delete(k).then(function(ok) {
                    console.log('[OTA] Cache "' + k + '" effacé : ' + ok);
                  });
                }));
              }).then(function() {
                console.log('[OTA] Tous les caches effacés. Redirection vers ' + target);
              }).catch(function(err) {
                console.warn('[OTA] Erreur lors de l\'effacement des caches :', err);
              }).finally(function() {
                window.location.href = target;
              });
            } else {
              console.log('[OTA] API Cache non disponible. Redirection vers ' + target);
              window.location.href = target;
            }
          }

          function Init(){
          SetHautBas();
          LoadParaFixe();
          
        }
        function SetParaFixe(){
          GID("Bwifi").style.display= (F.ESP32_Type!=10) ? "inline-block": "none";
          GH("Version_actu", F.VersionTexte ? F.VersionTexte : F.VersionStocke);
          Set_Couleurs();
        }
    </script>
    <br>
    <small>Après une mise à jour un restart de l'ESP32 et un Ctrl+F5 pour vider le cache du navigateur sont recommandés.</small>
    <div id="pied"></div>
    <br>
    <script src="/ParaCommunJS"></script>
    <script src="/CommunCouleurJS"></script>
</body></html>
 
 )====";