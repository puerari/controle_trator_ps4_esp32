#include <Bluepad32.h>
#include <ESP32Servo.h>
#include <esp_system.h>
#include <Preferences.h>

Servo meuServo;
Servo meuMotor;
Servo meuBraco;
Servo meuConch;
const int pinoMotor = 18; //18
const int pinoBraco = 19; //19
const int pinoConch = 21; //21
const int pinoServo = 26; //26
const int ml = 32;
const int mr = 93;
const int center = (ml + mr) / 2;
float xAxis = 0;
float yAxis = 0;
bool sirene = false;
bool optionsAnterior = false;  // borda de subida do Options, ver calibraLimites()
bool shareAnterior = false;    // borda de subida do Share, ver reiniciaLimites()

// Limites do braco gravados na flash interna (NVS), para sobreviverem ao desligamento.
// Isso existe porque a calibragem e feita com o ESP na bateria/power bank, longe do PC:
// nao da para ler o monitor serial na hora. Grava-se ali, e o valor e lido no proximo
// boot ligado no computador.
Preferences prefs;
const char* NVS_ESPACO = "trator";

// Espaco SEPARADO de proposito: reiniciaLimites() chama prefs.clear() no espaco "trator",
// e o historico de resets nao pode ser apagado junto com a calibragem. Nao junte os dois.
const char* NVS_DIAG = "diag";
const int MAX_HIST_RESET = 20;  // quantos boots o historico guarda

// Largura de pulso do MG996R (braco e concha), em microssegundos.
const int MG996R_MIN_US = 500;
const int MG996R_MAX_US = 2500;

// Suavidade do braco e da concha: enquanto o botao esta apertado eles andam 'step' grau
// por tick do loop, e o tick e um quadro do servo (50 Hz = 20 ms). Um destino novo por
// quadro e o maximo de suavidade que um servo analogico aceita, e nao existe tempo morto
// entre um movimento e o seguinte.
// Velocidade = step * 1000 / tickLoop graus/s, hoje 50 graus/s. Para acelerar, aumente
// 'step'; para desacelerar, aumente 'tickLoop' (mas isso tambem deixa direcao e tracao
// menos responsivas, porque o tick e o ritmo de todo o controle).
const int step = 1;
const int tickLoop = 20;  // ms, um quadro do servo a 50 Hz

// Posicao de repouso, e ponto de referencia de que todos os limites do braco dependem.
//
// Vale 175, nao 90: com o braco TOTALMENTE ABAIXADO em repouso, o servo nao sustenta peso
// nenhum parado, e o write() do setup() manda ele para onde a gravidade ja deixou o braco
// -- sem tranco e sem pico de corrente no boot. Com 90 o repouso ficava a 8 graus do topo
// e o servo segurava o braco no alto o tempo todo.
//
// 175 e nao 180 para sobrar folga do batente interno do servo; encostado no extremo o
// MG996R fica forcando contra o proprio limite.
//
// AO MONTAR: ligue o ESP primeiro, deixe o servo assentar em posBraco, e so entao encaixe
// o braco na posicao mais baixa, sem forcar. E assim que repouso e fundo coincidem.
const int posBraco = 175;

// O braco esta montado invertido: angulo MENOR = braco mais alto. Por isso R1 (sobe)
// decrementa R e para em minR, enquanto R2 (desce) incrementa e para em maxR.
//
// O curso e em GRAUS a partir do repouso, de proposito independente de 'step' (que vale
// 1 grau por tick, nao um passo de 10).
//
// cursoSobe = 44 foi MEDIDO com o braco montado: subiu em toques de R1 ate o batente e
// capturou com Options. Fechar esta janela nao e cosmetico -- com ela aberta em 170 o
// software deixava o R1 empurrar o servo 87 graus alem do batente, travando o MG996R em
// stall. Era uma das fontes de brownout.
//
// cursoDesce = 0 e proposital: o repouso JA e o ponto mais baixo, entao R2 nao tem para
// onde descer. Ele so serve para trazer o braco de volta ao repouso depois que R1 subiu.
const int cursoSobe = 44;   // limite do R1, medido
const int cursoDesce = 0;   // limite do R2: o repouso e o fundo

// Repouso da CONCHA, no MEIO do curso -- diferente do braco, que descansa no fundo. Por
// isso ela tem folga para os dois lados e tem DOIS limites a medir.
//
// A concha e invertida, igual ao braco: angulo MENOR = concha mais alta. L1 (sobe)
// decrementa L e para em minL, L2 (desce) incrementa e para em maxL. Os dois atuadores
// seguem a mesma convencao: minX e o limite de subida, maxX o de descida.
//
// Os dois cursos foram MEDIDOS com a concha montada: 25 graus para cima (concha em 65) e
// 23 para baixo (em 113), 48 no total, bem centrados no repouso. Para medir, cada janela foi
// aberta em 85 e fechada depois -- nao se mede um limite que esta fora do limite vigente, e
// com 85 aberto o L1/L2 podia empurrar o servo uns 60 graus alem do batente, travando o
// MG996R em stall.
//
// A inversao de L1/L2 nao mexeu nos angulos, so em qual botao chega a cada um. O que estava
// registrado como limite do L1 (113) era na verdade o fundo.
//
// O limite superior foi medido duas vezes: 70 na primeira, 65 na segunda. Se 65 estiver um
// pouco alem do batente real, o servo fica em leve stall parado no alto. Se aparecer B no
// historico de resets com a concha parada em cima, e este o suspeito.
//
// PARA REMEDIR: abrir a constante NAO basta. A NVS guarda o angulo medido e, se posConcha
// nao mudar, o posRefC continua batendo e a NVS vence no boot. Aperte SHARE antes de medir.
// Restaurar dos valores do codigo devolve os mesmos limites do braco, entao ele nao se perde.
//
// AO MONTAR: ligue o ESP primeiro, deixe o servo assentar em posConcha, e so entao encaixe
// a concha no meio do curso dela, sem forcar.
//
// ATENCAO: repouso no meio significa que, parada, a concha e sustentada pelo servo contra a
// gravidade -- consumo continuo, ao contrario do braco. Numa alimentacao no limite isso
// conta. Em troca, o tranco maximo no boot cai pela metade, porque o repouso fica no centro.
const int posConcha = 90;
const int cursoSobeConcha = 25;   // limite do L1, medido (concha em 65)
const int cursoDesceConcha = 23;  // limite do L2, medido (concha em 113)

// Nao sao const: Options captura a posicao atual como limite (ver calibraLimites).
int minR = posBraco - cursoSobe;   // 175 - 44 = 131
int maxR = posBraco + cursoDesce;  // 175 + 0 = 175, o proprio repouso
int minL = posConcha - cursoSobeConcha;   // 90 - 25 = 65, limite de SUBIDA
int maxL = posConcha + cursoDesceConcha;  // 90 + 23 = 113, limite de DESCIDA

int L = posConcha;
int R = posBraco;

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
  // Apaga a barra de luz uma vez, aqui. Isso ficava no inicio do dumpCar(), ou seja a cada
  // passada do loop; com o tick de 20 ms viraria 50 relatorios Bluetooth por segundo.
  ctl->setColorLED(0, 0, 0);

  bool foundEmptySlot = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
      // Additionally, you can get certain gamepad properties like:
      // Model, VID, PID, BTAddr, flags, etc.
      ControllerProperties properties = ctl->getProperties();
      Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                    properties.product_id);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }
  if (!foundEmptySlot) {
    Serial.println("CALLBACK: Controller connected, but could not found empty slot");
  }
}

void onDisconnectedController(ControllerPtr ctl) {
  bool foundController = false;

  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
      myControllers[i] = nullptr;
      foundController = true;
      break;
    }
  }

  if (!foundController) {
    Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
  }
}

void dumpMouse(ControllerPtr ctl) {
  Serial.printf("idx=%d, buttons: 0x%04x, scrollWheel=0x%04x, delta X: %4d, delta Y: %4d\n",
                ctl->index(),        // Controller Index
                ctl->buttons(),      // bitmask of pressed buttons
                ctl->scrollWheel(),  // Scroll Wheel
                ctl->deltaX(),       // (-511 - 512) left X Axis
                ctl->deltaY()        // (-511 - 512) left Y axis
  );
}

void dumpKeyboard(ControllerPtr ctl) {
  static const char* key_names[] = {
    // clang-format off
        // To avoid having too much noise in this file, only a few keys are mapped to strings.
        // Starts with "A", which is offset 4.
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
        // Special keys
        "Enter", "Escape", "Backspace", "Tab", "Spacebar", "Underscore", "Equal", "OpenBracket", "CloseBracket",
        "Backslash", "Tilde", "SemiColon", "Quote", "GraveAccent", "Comma", "Dot", "Slash", "CapsLock",
        // Function keys
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        // Cursors and others
        "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown",
        "RightArrow", "LeftArrow", "DownArrow", "UpArrow",
    // clang-format on
  };
  static const char* modifier_names[] = {
    // clang-format off
        // From 0xe0 to 0xe7
        "Left Control", "Left Shift", "Left Alt", "Left Meta",
        "Right Control", "Right Shift", "Right Alt", "Right Meta",
    // clang-format on
  };
  Serial.printf("idx=%d, Pressed keys: ", ctl->index());
  for (int key = Keyboard_A; key <= Keyboard_UpArrow; key++) {
    if (ctl->isKeyPressed(static_cast<KeyboardKey>(key))) {
      const char* keyName = key_names[key - 4];
      Serial.printf("%s,", keyName);
    }
  }
  for (int key = Keyboard_LeftControl; key <= Keyboard_RightMeta; key++) {
    if (ctl->isKeyPressed(static_cast<KeyboardKey>(key))) {
      const char* keyName = modifier_names[key - 0xe0];
      Serial.printf("%s,", keyName);
    }
  }
  Console.printf("\n");
}

void dumpBalanceBoard(ControllerPtr ctl) {
  Serial.printf("idx=%d,  TL=%u, TR=%u, BL=%u, BR=%u, temperature=%d\n",
                ctl->index(),        // Controller Index
                ctl->topLeft(),      // top-left scale
                ctl->topRight(),     // top-right scale
                ctl->bottomLeft(),   // bottom-left scale
                ctl->bottomRight(),  // bottom-right scale
                ctl->temperature()   // temperature: used to adjust the scale value's precision
  );
}

void processMouse(ControllerPtr ctl) {
  // This is just an example.
  if (ctl->scrollWheel() > 0) {
    // Do Something
  } else if (ctl->scrollWheel() < 0) {
    // Do something else
  }

  // See "dumpMouse" for possible things to query.
  dumpMouse(ctl);
}

void processKeyboard(ControllerPtr ctl) {
  if (!ctl->isAnyKeyPressed())
    return;

  // This is just an example.
  if (ctl->isKeyPressed(Keyboard_A)) {
    // Do Something
    Serial.println("Key 'A' pressed");
  }

  // Don't do "else" here.
  // Multiple keys can be pressed at the same time.
  if (ctl->isKeyPressed(Keyboard_LeftShift)) {
    // Do something else
    Serial.println("Key 'LEFT SHIFT' pressed");
  }

  // Don't do "else" here.
  // Multiple keys can be pressed at the same time.
  if (ctl->isKeyPressed(Keyboard_LeftArrow)) {
    // Do something else
    Serial.println("Key 'Left Arrow' pressed");
  }

  // See "dumpKeyboard" for possible things to query.
  dumpKeyboard(ctl);
}

void processBalanceBoard(ControllerPtr ctl) {
  // This is just an example.
  if (ctl->topLeft() > 10000) {
    // Do Something
  }

  // See "dumpBalanceBoard" for possible things to query.
  dumpBalanceBoard(ctl);
}

void processGamepad(ControllerPtr ctl) {
  // There are different ways to query whether a button is pressed.
  // By query each button individually:
  //  a(), b(), x(), y(), l1(), etc...
  /*if (ctl->a()) {
    static int colorIdx = 0;
    // Some gamepads like DS4 and DualSense support changing the color LED.
    // It is possible to change it by calling:
    switch (colorIdx % 3) {
      case 0:
        // Red
        ctl->setColorLED(255, 0, 0);
        break;
      case 1:
        // Green
        ctl->setColorLED(0, 255, 0);
        break;
      case 2:
        // Blue
        ctl->setColorLED(0, 0, 255);
        break;
    }
    colorIdx++;
  }

  if (ctl->b()) {
    // Turn on the 4 LED. Each bit represents one LED.
    static int led = 0;
    led++;
    // Some gamepads like the DS3, DualSense, Nintendo Wii, Nintendo Switch
    // support changing the "Player LEDs": those 4 LEDs that usually indicate
    // the "gamepad seat".
    // It is possible to change them by calling:
    ctl->setPlayerLEDs(led & 0x0f);
  }*/

  //if (ctl->x()) {
  // Some gamepads like DS3, DS4, DualSense, Switch, Xbox One S, Stadia support rumble.
  // It is possible to set it by calling:
  // Some controllers have two motors: "strong motor", "weak motor".
  // It is possible to control them independently.
  //ctl->playDualRumble(0 /* delayedStartMs */, 250 /* durationMs */, 0x80 /* weakMagnitude */, 0x40 /* strongMagnitude */);
  //}

  // Another way to query controller data is by getting the buttons() function.
  // See how the different "dump*" functions dump the Controller info.
  //dumpGamepad(ctl);
  dumpCar(ctl);
}

/*void stopBuzzer(ControllerPtr ctl) {
  if (ctl->buttons() == 4) {  // Square
    sirene = false;
    noTone(buzzerPin);
  }
}*/

void dumpGamepad(ControllerPtr ctl) {
  Serial.printf(
    "idx=%d, dpad: 0x%02x, buttons: 0x%04x, axis L: %4d, %4d, axis R: %4d, %4d, brake: %4d, throttle: %4d, "
    "misc: 0x%02x, gyro x:%6d y:%6d z:%6d, accel x:%6d y:%6d z:%6d\n",
    ctl->index(),        // Controller Index
    ctl->dpad(),         // D-pad
    ctl->buttons(),      // bitmask of pressed buttons
    ctl->axisX(),        // (-511 - 512) left X Axis
    ctl->axisY(),        // (-511 - 512) left Y axis
    ctl->axisRX(),       // (-511 - 512) right X axis
    ctl->axisRY(),       // (-511 - 512) right Y axis
    ctl->brake(),        // (0 - 1023): brake button
    ctl->throttle(),     // (0 - 1023): throttle (AKA gas) button
    ctl->miscButtons(),  // bitmask of pressed "misc" buttons
    ctl->gyroX(),        // Gyro X
    ctl->gyroY(),        // Gyro Y
    ctl->gyroZ(),        // Gyro Z
    ctl->accelX(),       // Accelerometer X
    ctl->accelY(),       // Accelerometer Y
    ctl->accelZ()        // Accelerometer Z
  );
}

// O tick e curto, entao logar toda passada inundaria o monitor serial. Loga a cada 10
// graus, que e a granularidade que o log tinha antes. Limite que nao caia em multiplo de
// 10 nao aparece no log.
void logaAngulo(const char* rotulo, int angulo) {
  if (angulo % 10 == 0)
    Serial.printf("%s=%d\n", rotulo, angulo);
}

// Grava os limites do braco na NVS. Sobrescrever o mesmo valor nao gasta flash: o
// nvs_set_i32 por baixo compara antes e ignora escrita identica.
void salvaLimites() {
  if (!prefs.begin(NVS_ESPACO, false)) {
    Serial.println("Falha ao abrir a NVS para gravar");
    return;
  }
  // Cada par guarda o repouso de que depende, para poder ser invalidado sozinho.
  prefs.putInt("posRefR", posBraco);
  prefs.putInt("minR", minR);
  prefs.putInt("maxR", maxR);
  prefs.putInt("posRefC", posConcha);
  prefs.putInt("minL", minL);
  prefs.putInt("maxL", maxL);
  prefs.end();
}

// Le os limites gravados, se houver, e imprime o que ficou valendo. Chamada no boot: e por
// esta linha que se descobre o que foi capturado longe do computador.
void carregaLimites() {
  bool bracoNvs = false, conchaNvs = false, descartado = false;
  // Aberto para escrita porque pode precisar descartar calibragem velha.
  if (prefs.begin(NVS_ESPACO, false)) {
    // Cada par e validado contra o repouso de que foi medido; limites tirados a partir de
    // outro repouso nao querem dizer mais nada. Sem este descarte, mudar posBraco ou
    // posConcha no codigo nao surtia efeito algum -- a NVS vencia em silencio e so um Share
    // salvava. Ja mordeu duas vezes. Os pares sao removidos separadamente, para invalidar a
    // calibragem do braco nao levar a da concha junto.
    if (prefs.isKey("minR")) {
      if (prefs.getInt("posRefR", -1) == posBraco) {
        minR = prefs.getInt("minR", minR);
        maxR = prefs.getInt("maxR", maxR);
        bracoNvs = true;
      } else {
        prefs.remove("minR");
        prefs.remove("maxR");
        prefs.remove("posRefR");
        descartado = true;
      }
    }
    if (prefs.isKey("minL")) {
      if (prefs.getInt("posRefC", -1) == posConcha) {
        minL = prefs.getInt("minL", minL);
        maxL = prefs.getInt("maxL", maxL);
        conchaNvs = true;
      } else {
        prefs.remove("minL");
        prefs.remove("maxL");
        prefs.remove("posRefC");
        descartado = true;
      }
    }
    prefs.end();
  }
  if (descartado)
    Serial.println("Calibragem descartada: foi medida a partir de outro ponto de repouso");
  Serial.printf("Braco  (%s): repouso=%d | sobe ate %d (cursoSobe=%d) | desce ate %d (cursoDesce=%d)\n",
                bracoNvs ? "NVS" : "codigo", posBraco,
                minR, posBraco - minR, maxR, maxR - posBraco);
  Serial.printf("Concha (%s): repouso=%d | sobe ate %d (cursoSobeConcha=%d) | desce ate %d (cursoDesceConcha=%d)\n",
                conchaNvs ? "NVS" : "codigo", posConcha,
                minL, posConcha - minL, maxL, maxL - posConcha);
}

// Captura a posicao ATUAL do braco E da concha como limite do lado a que cada um esta
// deslocado, grava na NVS e confirma com vibracao no controle -- a confirmacao e tatil de
// proposito, porque durante a calibragem o ESP esta no power bank e nao ha monitor serial.
//
// Quem estiver no proprio repouso e ignorado, entao na pratica so e capturado o que voce
// acabou de mover. Mover os dois e apertar Options captura os dois, o que tambem esta certo.
//
// Nao existe leitura da posicao real do servo: um MG996R e um servo de 3 fios, o
// potenciometro interno nao sai no conector, e Servo::read() da ESP32Servo apenas ecoa o
// ultimo valor escrito (readMicroseconds() devolve this->ticks, nunca consulta o servo).
// Entao R e L sao a melhor referencia disponivel: acompanham o servo de perto porque so
// andam de 1 em 1 grau por tick. Se algo for movido a mao, travar ou escorregar sob carga, a
// variavel deixa de valer e so reiniciar ressincroniza.
//
// O valor sobrevive ao desligamento. Para tornar definitivo no codigo, edite as constantes
// de curso com os numeros que aparecem no boot seguinte.
void calibraLimites(ControllerPtr ctl) {
  bool capturou = false;

  if (R < posBraco) {  // braco acima do repouso: limite de SUBIDA
    minR = R;
    Serial.printf("Braco, subida: minR=%d (cursoSobe = %d)\n", minR, posBraco - minR);
    capturou = true;
  } else if (R > posBraco) {  // abaixo do repouso: limite de DESCIDA
    maxR = R;
    Serial.printf("Braco, descida: maxR=%d (cursoDesce = %d)\n", maxR, maxR - posBraco);
    capturou = true;
  }

  // A concha e invertida, igual ao braco: angulo menor = concha mais alta.
  if (L < posConcha) {
    minL = L;
    Serial.printf("Concha, subida: minL=%d (cursoSobeConcha = %d)\n", minL, posConcha - minL);
    capturou = true;
  } else if (L > posConcha) {
    maxL = L;
    Serial.printf("Concha, descida: maxL=%d (cursoDesceConcha = %d)\n", maxL, maxL - posConcha);
    capturou = true;
  }

  if (!capturou) {
    Serial.println("Braco e concha nos pontos de repouso. Mova um deles antes de capturar.");
    ctl->playDualRumble(0, 100, 0x40, 0x00);  // fraco e curto: nada capturado
    return;
  }

  salvaLimites();
  ctl->playDualRumble(0, 300, 0x40, 0xC0);  // forte e longo: gravado na NVS
}

// Desfaz a calibragem: volta aos valores do codigo e limpa a NVS.
//
// Isto NAO e opcional. Um limite capturado curto demais se auto-tranca: o braco nao
// consegue passar dele, entao nao ha como leva-lo ate o limite real para recapturar um
// valor maior. Sem esta saida, uma captura ruim so se desfaz regravando o firmware.
void reiniciaLimites(ControllerPtr ctl) {
  minR = posBraco - cursoSobe;
  maxR = posBraco + cursoDesce;
  minL = posConcha - cursoSobeConcha;
  maxL = posConcha + cursoDesceConcha;
  if (prefs.begin(NVS_ESPACO, false)) {
    prefs.clear();  // so o espaco "trator"; o historico de resets vive em NVS_DIAG
    prefs.end();
  }
  Serial.printf("Limites restaurados do codigo: braco %d..%d | concha %d..%d\n",
                minR, maxR, minL, maxL);
  // dois pulsos curtos, para nao confundir com a captura (que e um pulso longo).
  // O delayedStartMs agenda o segundo sem bloquear o loop.
  ctl->playDualRumble(0, 120, 0x00, 0x60);
  ctl->playDualRumble(260, 120, 0x00, 0x60);
}

void dumpCar(ControllerPtr ctl) {
  // Options captura os limites do braco e da concha. Borda de subida, senao repetiria a
  // cada tick enquanto o botao estiver apertado.
  bool options = ctl->miscButtons() & MISC_BUTTON_START;
  if (options && !optionsAnterior)
    calibraLimites(ctl);
  optionsAnterior = options;

  // Share desfaz a calibragem e volta aos limites do codigo.
  bool share = ctl->miscButtons() & MISC_BUTTON_SELECT;
  if (share && !shareAnterior)
    reiniciaLimites(ctl);
  shareAnterior = share;

  if (ctl->buttons() == 1) { // X
    meuMotor.write(0);  //para trás
  } else if(ctl->buttons() == 8) { //Triangle
    meuMotor.write(180);  //para frente
  } else {
    meuMotor.write(90); //parar
  }

  if (ctl->buttons() == 16) { // L1
    if (L > minL)
      L -= step;
    meuConch.write(L); //sobe
    logaAngulo("L1 concha", L);
  } else if(ctl->buttons() == 64) { // L2
    if (L < maxL)
      L += step;
    meuConch.write(L); //desce
    logaAngulo("L2 concha", L);
  }

  if (ctl->buttons() == 32) { // R1
    if (R > minR)
      R -= step;
    meuBraco.write(R); //sobe
    logaAngulo("R1 braco", R);
  } else if(ctl->buttons() == 128) { // R2
    if (R < maxR)
      R += step;
    meuBraco.write(R); // desce
    logaAngulo("R2 braco", R);
  }

  /*switch (ctl->buttons()) {
    case 1:               //X
      meuMotor.write(0);  //para trás
      break;
    //case 2:  //Circle
    //case 4:  //Square
    case 8:                 //Triangle
      meuMotor.write(180);  //para frente
      break;
    case 16:  //L1
      meuConch.write(180);
      Serial.println("L1");
      break;
    case 64:  //L2
      meuConch.write(70);
      Serial.println("L2");
      break;
    case 32:  //R1
      meuBraco.write(180);
      Serial.println("R1");
      break;
    case 128:  //R2
      meuBraco.write(80);
      Serial.println("R2");
      break;
    default:
      meuMotor.write(90);
      //meuServo.write(center);
      //noTone(buzzerPin);
  }*/

  switch (ctl->dpad()) {
    case 1:                 //Up
      meuMotor.write(180);  //para frente
      break;
    case 2:               //Down
      meuMotor.write(0);  //para frente
      break;
    case 4:  //Right
      meuServo.write(mr);
      break;
    case 8:  //Left
      meuServo.write(ml);
      break;
      //default:
      //meuMotor.write(90);
      //meuServo.write(center);
  }
  if (ctl->dpad() != 4 && ctl->dpad() != 8) {
    xAxis = ctl->axisX();  // (-511 - 512) left X Axis
    meuServo.write(map(xAxis, -511, 512, ml, mr));
  }
  if (ctl->dpad() != 1 && ctl->dpad() != 2 && ctl->buttons() != 1 && ctl->buttons() != 8) {
    yAxis = ctl->axisRY();  // (-511 - 512) left Y axis
    meuMotor.write(map(yAxis, -511, 512, 180, 0));
  }

  //Buzzer
  /*if (ctl->buttons() == 2 || sirene) { //Circle
    sirene = true;
    stopBuzzer(ctl);
    for (int i = 0; i < 255 && sirene; i++) {  // Aumenta a frequência
      tone(buzzerPin, 1000 + i * 20, 10);      // Varia o tom de 1000Hz para 5000Hz
      delay(5);                                // Pequeno delay para variar a frequência
      stopBuzzer(ctl);
    }
    for (int i = 255; i > 0 && sirene; i--) {  // Diminui a frequência
      tone(buzzerPin, 1000 + i * 20, 10);      // Varia o tom de 5000Hz para 1000Hz
      delay(5);
      stopBuzzer(ctl);
    }
  } else {
    noTone(buzzerPin);
  }*/
}

void processControllers() {
  for (auto myController : myControllers) {
    if (myController && myController->isConnected() && myController->hasData()) {
      if (myController->isGamepad()) {
        processGamepad(myController);
      } else if (myController->isMouse()) {
        processMouse(myController);
      } else if (myController->isKeyboard()) {
        processKeyboard(myController);
      } else if (myController->isBalanceBoard()) {
        processBalanceBoard(myController);
      } else {
        Serial.println("Unsupported controller");
      }
    }
  }
}

// Diagnostico: imprime por que o ESP reiniciou. BROWNOUT/POWERON apontam para queda
// de tensao (servo puxando corrente demais); PANIC/WDT apontam para falha de software.
void mostrarMotivoReset() {
  esp_reset_reason_t motivo = esp_reset_reason();
  const char* nome;
  switch (motivo) {
    case ESP_RST_POWERON:   nome = "POWERON (energia ligada ou queda total de tensao)"; break;
    case ESP_RST_BROWNOUT:  nome = "BROWNOUT (tensao caiu abaixo do limite)"; break;
    case ESP_RST_PANIC:     nome = "PANIC (excecao de software)"; break;
    case ESP_RST_TASK_WDT:  nome = "TASK_WDT (watchdog de tarefa)"; break;
    case ESP_RST_INT_WDT:   nome = "INT_WDT (watchdog de interrupcao)"; break;
    case ESP_RST_WDT:       nome = "WDT (outro watchdog)"; break;
    case ESP_RST_SW:        nome = "SW (reset por software)"; break;
    case ESP_RST_EXT:       nome = "EXT (pino de reset / botao EN)"; break;
    case ESP_RST_DEEPSLEEP: nome = "DEEPSLEEP"; break;
    case ESP_RST_SDIO:      nome = "SDIO"; break;
    default:                nome = "UNKNOWN"; break;
  }
  Serial.printf("Motivo do reset: %d - %s\n", (int)motivo, nome);
  Serial.printf("Heap livre: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

// Um caractere por motivo, para o historico caber em pouca coisa e ser legivel de relance.
char codigoReset(esp_reset_reason_t motivo) {
  switch (motivo) {
    case ESP_RST_POWERON:   return 'P';
    case ESP_RST_BROWNOUT:  return 'B';
    case ESP_RST_PANIC:     return 'X';
    case ESP_RST_TASK_WDT:  return 'W';
    case ESP_RST_INT_WDT:   return 'W';
    case ESP_RST_WDT:       return 'W';
    case ESP_RST_SW:        return 'S';
    case ESP_RST_EXT:       return 'E';
    case ESP_RST_DEEPSLEEP: return 'D';
    default:                return '?';
  }
}

// Grava na NVS quantos boots o ESP ja teve e o motivo dos ultimos MAX_HIST_RESET.
//
// Por que HISTORICO e nao "ultimo motivo": abrir a porta serial reseta a placa (ver
// CLAUDE.md). Um slot unico seria sobrescrito por esse proprio reset, exatamente na hora de
// ler -- foi assim que se perdeu a evidencia de um reset ocorrido em campo.
//
// Por que TAMBEM um contador de boots: uma queda de tensao severa apaga o dominio RTC e se
// apresenta como POWERON, indistinguivel de ligar na tomada. O que denuncia o problema nao
// e o motivo, e a contagem -- se voce ligou uma vez e surgiram cinco boots, houve quatro
// resets que ninguem pediu.
//
// Chamada no inicio do setup(), de proposito ANTES do posicionamento dos servos: se a
// escrita no servo derrubar a placa, este boot ja esta registrado.
void registraReset() {
  if (!prefs.begin(NVS_DIAG, false)) {
    Serial.println("Falha ao abrir a NVS de diagnostico");
    return;
  }
  int boots = prefs.getInt("boots", 0) + 1;
  prefs.putInt("boots", boots);

  String hist = prefs.getString("hist", "");
  hist += codigoReset(esp_reset_reason());
  while (hist.length() > MAX_HIST_RESET)
    hist = hist.substring(1);  // descarta o mais antigo
  prefs.putString("hist", hist);
  prefs.end();

  Serial.printf("Boot #%d | resets antigo->recente: %s\n", boots, hist.c_str());
  Serial.println("  P=poweron B=brownout X=panic W=watchdog S=software E=pino D=deepsleep ?=outro");
}

// Arduino setup function. Runs in CPU 1
void setup() {
  Serial.begin(115200);
  delay(300);  // da tempo do monitor serial abrir antes do diagnostico
  mostrarMotivoReset();
  registraReset();
  carregaLimites();
  Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t* addr = BP32.localBdAddress();
  Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

  // "forgetBluetoothKeys()" should be called when the user performs
  // a "device factory reset", or similar.
  // Calling "forgetBluetoothKeys" in setup() just as an example.
  // Forgetting Bluetooth keys prevents "paired" gamepads to reconnect.
  // But it might also fix some connection / re-connection issues.
  //BP32.forgetBluetoothKeys();

  meuServo.attach(pinoServo);
  meuMotor.attach(pinoMotor);
  // Braco e concha sao MG996R. O attach(pin) da ESP32Servo usa 544-2400us, que nao
  // cobre o curso completo do MG996R; 500-2500us da os 180 graus.
  meuBraco.attach(pinoBraco, MG996R_MIN_US, MG996R_MAX_US);
  meuConch.attach(pinoConch, MG996R_MIN_US, MG996R_MAX_US);

  // Posicao inicial. Depois do attach o servo ainda nao recebe pulso, entao a posicao
  // fisica dele e desconhecida e o primeiro R1/L1 daria um salto de tamanho imprevisivel.
  // Mandar o repouso aqui sincroniza R e L com os servos, e dai em diante todo movimento e
  // um passo de 'step' em rampa. Um servo por vez, para nao somar os picos de corrente.
  // Feito antes de BP32.setup() para nao coincidir com o radio Bluetooth subindo.
  meuBraco.write(R);
  delay(500);
  meuConch.write(L);
  delay(500);

  // Setup the Bluepad32 callbacks
  BP32.setup(&onConnectedController, &onDisconnectedController);

  // Enables mouse / touchpad support for gamepads that support them.
  // When enabled, controllers like DualSense and DualShock4 generate two connected devices:
  // - First one: the gamepad
  // - Second one, which is a "virtual device", is a mouse.
  // By default, it is disabled.
  BP32.enableVirtualDevice(false);

  //pinMode(buzzerPin, OUTPUT);
}

// Arduino loop function. Runs in CPU 1.
void loop() {

  // This call fetches all the controllers' data.
  // Call this function in your main loop.
  bool dataUpdated = BP32.update();
  if (dataUpdated)
    processControllers();

  // The main loop must have some kind of "yield to lower priority task" event.
  // Otherwise, the watchdog will get triggered.
  // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
  // Detailed info here:
  // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

  //     vTaskDelay(1);
  delay(tickLoop);
}
