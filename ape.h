/**
  This classes were created by glmnet (https://github.com/glmnet/esphome_devices)
  Many thanks!
*/

// Must disable debug if using debug main.cpp or in other custom components for the
//  __c causes a section type conflict with __c thingy

// #define APE_DEBUG

static const char *TAGape = "ape";

#define APE_CMD_DIGITAL_READ 0
#define APE_CMD_WRITE_ANALOG 2
#define APE_CMD_WRITE_DIGITAL_HIGH 3
#define APE_CMD_WRITE_DIGITAL_LOW 4
#define APE_CMD_SETUP_PIN_OUTPUT 5
#define APE_CMD_SETUP_PIN_INPUT_PULLUP 6
#define APE_CMD_SETUP_PIN_INPUT 7
// 8 analog registers.. A0 to A7
// A4 and A5 not supported due to I2C
#define CMD_ANALOG_READ_A0 0b1000 // 0x8
// ....
#define CMD_ANALOG_READ_A7 0b1111 // 0xF

#define CMD_SETUP_ANALOG_INTERNAL 0x10
#define CMD_SETUP_ANALOG_OTHER 0x11

#define get_ape(constructor) static_cast<ArduinoPortExtender *>(const_cast<custom_component::CustomComponentConstructor *>(&constructor)->get_component(0))

#define ape_switch(ape, pin) get_ape(ape)->get_switch(pin)
#define ape_binary_sensor(ape, pin) get_ape(ape)->get_binary_sensor(pin)
//#define ape_sensor(ape, pin) get_ape(ape)->get_sensor(pin)



class ArduinoPortExtender;

using namespace esphome;

class ApeSwitch : public switch_::Switch
{
public:
  ApeSwitch(ArduinoPortExtender *parent, uint8_t pin)
  {
    this->parent_ = parent;
    this->pin_ = pin;
  }
  void write_state(bool state) override;
  uint8_t get_pin() { return this->pin_; }

protected:
  ArduinoPortExtender *parent_;
  uint8_t pin_;
  // Pins are setup as output after the state is written, Arduino has no open drain outputs, after setting an output it will either sink or source thus activating outputs writen to false during a flick.
  bool setup_{true};
  bool state_{false};

  friend class ArduinoPortExtender;
};


class ApeBinarySensor : public binary_sensor::BinarySensor
{
public:
  ApeBinarySensor(ArduinoPortExtender *parent, uint8_t pin)
  {
    this->pin_ = pin;
  }
  uint8_t get_pin() { return this->pin_; }

protected:
  uint8_t pin_;
};
/*
class ApeSensor : public sensor::Sensor
{
  public:
    ApeSensor(ArduinoPortExtender *parent, uint8_t pin)
    {
        this->pin_ = pin;
    }
    uint8_t get_pin() { return this->pin_; }

  protected:
    uint8_t pin_;
};*/

class ArduinoPortExtender : public Component, public I2CDevice
{
public:
  ArduinoPortExtender(I2CComponent *parent, uint8_t address) : I2CDevice(parent, address)
  {
  }
  void setup() override
  {
#ifdef APE_DEBUG
    ESP_LOGCONFIG(TAGape, "Setting up ArduinoPortExtender at %#02x ...", address_);
#endif

    /* We cannot setup as usual as arduino boots later than esp8266

            Poll i2c bus for our Arduino for a n seconds instead of failing fast,
            also this is important as pin setup (INPUT_PULLUP, OUTPUT it's done once)
        */
    this->configure_timeout_ = millis() + 5000;
  }
  void loop() override
  {
    if (millis() < this->configure_timeout_)
    {
      bool try_configure = millis() % 100 > 50;
      if (try_configure == this->configure_)
        return;
      this->configure_ = try_configure;

      if (this->read_bytes(APE_CMD_DIGITAL_READ, const_cast<uint8_t *>(this->read_buffer_), 3, 1))
      {
#ifdef APE_DEBUG
        ESP_LOGCONFIG(TAGape, "ArduinoPortExpander found at %#02x", address_);
#endif
        delay(10);

        // Config success
        this->configure_timeout_ = 0;
        this->status_clear_error();

        for (ApeBinarySensor *pin : this->input_pins_)
        {
          App.feed_wdt();
          uint8_t pinNo = pin->get_pin();
#ifdef APE_DEBUG
          ESP_LOGCONFIG(TAGape, "Setup input pin %d", pinNo);
#endif
          this->write_byte(APE_CMD_SETUP_PIN_INPUT_PULLUP, pinNo);
          delay(20);
        }
        for (ApeSwitch *output : this->output_pins_)
        {
          if (!output->setup_) { // this output has a valid value already
            this->write_state(output->pin_, output->state_, true);
            App.feed_wdt();
            delay(20);
          }
        }
        return;
      }
      // Still not answering
      return;
    }
    if (this->configure_timeout_ != 0 && millis() > this->configure_timeout_)
    {
#ifdef APE_DEBUG
      ESP_LOGE(TAGape, "ArduinoPortExpander NOT found at %#02x", address_);
#endif
      this->mark_failed();
      return;
    }

    if (!this->read_bytes(APE_CMD_DIGITAL_READ, const_cast<uint8_t *>(this->read_buffer_), 3, 1))
    {
#ifdef APE_DEBUG
      ESP_LOGE(TAGape, "Error reading. Reconfiguring pending.");
#endif
      this->status_set_error();
      this->configure_timeout_ = millis() + 5000;
      return;
    }
    for (ApeBinarySensor *pin : this->input_pins_)
    {
      uint8_t pinNo = pin->get_pin();

      uint8_t bit = pinNo % 8;
      uint8_t value = pinNo < 8 ? this->read_buffer_[0] : pinNo < 16 ? this->read_buffer_[1] : this->read_buffer_[2];
      bool ret = value & (1 << bit);
      if (this->initial_state_)
        pin->publish_initial_state(ret);
      else
        pin->publish_state(ret);
    }
    this->initial_state_ = false;
  }

  /*   uint16_t analogRead(uint8_t pin)
    {
        this->read_bytes((uint8_t)(CMD_ANALOG_READ_A0 + pin), const_cast<uint8_t *>(this->read_buffer_), 2, 1);
        uint16_t value = this->read_buffer_[0] | ((uint16_t)this->read_buffer_[1] << 8);
        return value;
    }*/

  switch_::Switch *
  get_switch(uint8_t pin)
  {
    ApeSwitch *output = new ApeSwitch(this, pin);
    output_pins_.push_back(output);
    return output;
  }
  binary_sensor::BinarySensor *get_binary_sensor(uint8_t pin)
  {
    ApeBinarySensor *binarySensor = new ApeBinarySensor(this, pin);
    input_pins_.push_back(binarySensor);
    return binarySensor;
  }
  /*    sensor::Sensor *get_sensor(uint8_t pin)
    {
        ApeSensor *sensor = new ApeSensor(this, pin);
        analog_pins_.push_back(sensor);
        return sensor;
    }*/

  void write_state(uint8_t pin, bool state, bool setup = false)
  {
    if (this->configure_timeout_ != 0) return;
#ifdef APE_DEBUG
    ESP_LOGD(TAGape, "Writing %d to pin %d", state, pin);
#endif
    this->write_byte(state ? APE_CMD_WRITE_DIGITAL_HIGH : APE_CMD_WRITE_DIGITAL_LOW, pin);
    if (setup)
    {
      App.feed_wdt();
      delay(20);
#ifdef APE_DEBUG
      ESP_LOGI(TAGape, "Setup output pin %d", pin);
#endif
      this->write_byte(APE_CMD_SETUP_PIN_OUTPUT, pin);
    }
  }

protected:
  bool configure_{true};
  bool initial_state_{true};
  uint8_t read_buffer_[3]{0, 0, 0};
  unsigned long configure_timeout_{5000};
  std::vector<ApeSwitch *> output_pins_;
  std::vector<ApeBinarySensor *> input_pins_;
  //std::vector<ApeSensor *> analog_pins_;
};

void ApeSwitch::write_state(bool state)
{
  this->state_ = state;
  this->parent_->write_state(this->pin_, state, this->setup_);
  publish_state(state);
  this->setup_ = false;
}

/*
class LM35Sensor : public PollingComponent, public sensor::Sensor
{
  public:
    LM35Sensor(ArduinoPortExtender *parent, uint8_t pin) : PollingComponent(2400)
    {
        this->parent_ = parent;
        this->pin_ = pin;
    }

    void setup() override
    {
        // This will be called by App.setup()
    }

    void update() override
    {
        // This will be called every "update_interval" milliseconds.

        // Takes a measuremant every 2.4 seconds, at number 10 publishes values
        // skips from 11 to 250 so efective readings every 600 seconds / 10 mins
        ++readings_;

        if (readings_ == 250)
            readings_ = 0;
        if (readings_ > 10)
            return;

        this->reading_ += this->parent_->analogRead(pin_);
        if (readings_ == 10)
        {
            float temp = (1.1 * this->reading_ * 10) / 1023;
            #ifdef APE_DEBUG
            ESP_LOGI(TAG, "Temperature: %.2f", temp);
            #endif
            this->reading_ = 0;
            publish_state(temp);
        }
    }

  protected:
    ArduinoPortExtender *parent_;
    uint8_t pin_;
    uint16_t reading_{0};
    uint8_t readings_{0};
};
*/