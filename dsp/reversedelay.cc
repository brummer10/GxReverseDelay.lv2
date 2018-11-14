#include <cmath>
#include <stdlib.h>	


namespace reversedelay {


class ReverseDelay : public PluginLV2 {
private:
    float sample_rate;
    float *buffer;
    uint32_t counter;
   	uint32_t buf_size;
    
    uint32_t cur_buf_size; 
	float feedback_buf;

    //Params
    float time, feedback, window, drywet;
    float *time_, *feedback_, *window_, *drywet_;
	//Params buffs
	float time_old, window_old;

    //Indicator
    float buf_indication;
    float *buf_indication_;

	class overlap_window
	{
	private:
		float val;
		float step;
		float acc;
		uint32_t active_samples;
		uint32_t full_samples;
		uint32_t counter;

	public:
		overlap_window() {
		    val = 0;
		    step = 0;
		    acc = 0;
		    active_samples = 0;
		    full_samples = 0;
		    counter = 0;
		}
		void set_coef(float t /* 0..1 */, uint32_t full_samples) {
		    set_full(0, full_samples, t*full_samples);
		}
		bool set_full(float min_val, uint32_t full_samples, uint32_t active_samples) {
		    if(active_samples >= full_samples) return false;

		    acc = min_val;
		    val = min_val;
		    this->full_samples = full_samples;
		    this->active_samples = active_samples;
		    counter = 0;
		    step = (1 - min_val)/(active_samples/2);

		    return true;
		}
		float get() {
		    if(counter >= 0 && counter < active_samples/2) {
		        acc += step;
		        counter++;
		        return acc;
		    }
		    else if(counter >= active_samples/2 && counter <= full_samples - active_samples/2) {
		        counter++;
		        return 1;
		    }
		    else if(counter > full_samples - active_samples/2 && counter < full_samples) {
		        acc -= step;
		        counter++;
		        return acc;
		    }
		    else if(counter >= full_samples) {
		        float ret_val = acc;
		        acc = val;
		        counter = 0;
		        return ret_val;
		    }
		    return 1;
		}
	};	

	overlap_window ow;

	static float reverse_delay_line_impl(float in, float* buf, uint32_t* counter, uint32_t length)
	{
		float out = 0;

		//Read data
		if(*counter < length - 1)
		{
		    uint32_t read_counter = length - 1 - (*counter);
		    out = buf[read_counter];
		}

		//Write data
		*(buf + (*counter)) = in;
		(*counter)++;
		if ((*counter) > length-1)
		    (*counter) = 0;

		return (out);
	}
	void compute(int count, FAUSTFLOAT *input0, FAUSTFLOAT *output0);

public:
    ReverseDelay();

	static void clear_state_f_static(PluginLV2*);
	static void init_static(uint32_t samplingFreq, PluginLV2*);
	static void compute_static(int count, FAUSTFLOAT *input0, FAUSTFLOAT *output0, PluginLV2*);
	static void del_instance(PluginLV2 *p);
	static void connect_static(uint32_t port,void* data, PluginLV2 *p);


};

ReverseDelay::ReverseDelay():
    PluginLV2(),
    sample_rate(0) {
    version = PLUGINLV2_VERSION;
    id = "reversedelay";
    name = N_("ReverseDelay");

	mono_audio = compute_static;
	stereo_audio = 0;
	set_samplerate = init_static;
	activate_plugin = 0;
	connect_ports = connect_static;
	clear_state = clear_state_f_static;
	delete_instance = del_instance;

    buffer = NULL;
    counter = 0;
    buf_size    = 0;
    cur_buf_size = 0;
    feedback_buf = 0;
	time_old = 0; 
	window_old = 0;

    buf_indication = 0;
}


void ReverseDelay::connect_static(uint32_t port,void* data, PluginLV2 *plugin)
{
	ReverseDelay& self = *static_cast<ReverseDelay*>(plugin);
	switch ((PortIndex)port)
	{
	case TIME: 
		self.time_ = (float*)data; // , 500, 200, 2000, 1
		break;
	case FFEEDBACK: 
		self.feedback_ = (float*)data; // , 0, 0, 1, 0.05
		break;
	case WINDOW: 
		self.window_ = (float*)data; // , 50, 0, 100, 1
		break;
	case DRYWET: 
		self.drywet_ = (float*)data; // , 0.5, 0, 1, 0.05
		break;
	case BUF_INDICATON: 
		self.buf_indication_ = (float*)data; // , 0.0, 0.0, 1.0, 0.01
		break;
	default:
		break;
	}
}

void ReverseDelay::clear_state_f_static(PluginLV2 *plugin)
{
	ReverseDelay& self = *static_cast<ReverseDelay*>(plugin);
	for (size_t i = 0; i < self.buf_size; i++)
		self.buffer[i] = 0.0f;
}

void ReverseDelay::init_static(uint32_t samplingFreq, PluginLV2 *plugin) {
	ReverseDelay& self = *static_cast<ReverseDelay*>(plugin);
    self.sample_rate = (float)samplingFreq;

	float* old_buf = self.buffer;

	//Provide dual buf size, with 2 seconds length for every part
	uint32_t new_buf_size = (uint32_t)(samplingFreq * 2 * 2);

	float *new_buf = new float[new_buf_size];
	for (size_t i = 0; i < new_buf_size; i++)
		new_buf[i] = 0.0f;

	// Assign new pointer and size
	self.buffer         = new_buf;
	self.buf_size       = new_buf_size;

	// Delete old buffer
	if (old_buf != NULL)
		delete [] old_buf;
}

void ReverseDelay::compute(int count, float *input, float *output) {

#define time (*time_)
#define feedback (*feedback_)
#define window (*window_)
#define drywet (*drywet_)
#define buf_indication (*buf_indication_)

    //Update params
	if(time_old != time) {
		cur_buf_size = (time/1000.0)*sample_rate;
		counter = 0;
		ow.set_coef((window)/(100.0 + 1.0), cur_buf_size/2); //Avoid to pass 1

		time_old = time;
		window_old = window;
	}
	else if(window_old != window)
	{ 
    	ow.set_coef((window)/(100.0 + 1.0), cur_buf_size/2); 
		window_old = window;
	}

    for (int i = 0; i < count; ++i) {
        float in = input[i];
        float out = 0;

        //Update indicator
        buf_indication = ((float)counter)/cur_buf_size;

        //Process
        out = reverse_delay_line_impl(in + feedback_buf * feedback, buffer, &counter, cur_buf_size);
        feedback_buf = out;

        out*= ow.get();

        out = out* drywet + in* (1 - drywet);
        output[i] = out;
    }
#undef time
#undef feedback
#undef window
#undef drywet
#undef buf_indication
}

void ReverseDelay::compute_static(int count, float *input, float *output, PluginLV2 *plugin) {
	static_cast<ReverseDelay*>(plugin)->compute(count, input, output);
}

void ReverseDelay::del_instance(PluginLV2 *plugin)
{ 
	ReverseDelay& self = *static_cast<ReverseDelay*>(plugin);
	delete [] self.buffer;
    delete static_cast<ReverseDelay*>(plugin);
}



PluginLV2 *plugin() {
    return new ReverseDelay;
}

/*
typedef enum
{
   TIME,
   FFEEDBACK,
   WINDOW,
   DRYWET,
   BUF_INDICATON,
} PortIndex;
*/

} // end namespace reverse_delay




