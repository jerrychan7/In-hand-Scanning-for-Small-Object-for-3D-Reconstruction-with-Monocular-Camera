

from PIL import Image
import base64
import numpy as np
from u2net import model
import torch
from torchvision import transforms
from skimage import transform
import time
from io import BytesIO
from flask_cors import CORS
import os
from flask import Flask, request, send_from_directory

app = Flask(__name__, static_folder='build/')
cors = CORS(app)

model_path = './u2net.pth'
model_pred = model.U2NET(3, 1)

from PIL import Image
import base64
import numpy as np
from u2net import model
import torch
from torchvision import transforms
from skimage import transform
import time
from io import BytesIO
from flask_cors import CORS
import os
from flask import Flask, request, send_from_directory

app = Flask(__name__, static_folder='build/')
cors = CORS(app)

model_path = './u2net.pth'
model_pred = model.U2NET(3, 1)

# normalize the predicted SOD probability map
def norm_pred(d):
    mi = torch.min(d)
    return (d - mi) / (torch.max(d) - mi)

class RescaleT(object):
    def __init__(self, output_size):
        assert isinstance(output_size, (int, ))
        self.output_size = output_size

    def __call__(self, sample):
        image = sample["image"]
        img = transform.resize(image, (self.output_size, self.output_size), mode='constant')
        return {"image": img}

class ToTensorLab(object):
    """Convert ndarrays in sample to Tensors."""
    def __init__(self,flag=0):
        self.flag = flag

    def __call__(self, sample):
        image=sample['image']
        # change the color space with rgb color
        tmpImg = np.zeros((image.shape[0],image.shape[1],3))
        image = image/np.max(image)
        if image.shape[2]==1:
            tmpImg[:,:,0] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,1] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,2] = (image[:,:,0]-0.485)/0.229
        else:
            tmpImg[:,:,0] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,1] = (image[:,:,1]-0.456)/0.224
            tmpImg[:,:,2] = (image[:,:,2]-0.406)/0.225

        tmpImg = tmpImg.transpose((2, 0, 1))

        return {'image': torch.from_numpy(tmpImg)}


def preprocess_img(image):
    transform = transforms.Compose([RescaleT(320), ToTensorLab(flag=0)])
    sample = transform({"image": image})

    return sample

@app.route('/segmentation', methods=['POST'])
def process():

    t = time.process_time_ns()

    with torch.no_grad():
        # 22 = len("data:image/png;base64,")
        img = Image.open(BytesIO(base64.b64decode(request.form.get("base64", "")[22:])))

        sample = preprocess_img(np.array(img))
        inputs_test = torch.FloatTensor(sample["image"].unsqueeze(0).float())
        if torch.cuda.is_available():
            inputs_test = torch.autograd.Variable(inputs_test.cuda())
        else:
            inputs_test = torch.autograd.Variable(inputs_test)
        d1, _, _, _, _, _, _ = model_pred(inputs_test)
        pred = d1[:, 0, :, :]
        predict = norm_pred(pred).squeeze().cpu().detach().numpy()
        img_out = Image.fromarray(predict * 255).convert("RGB")
        img_out = img_out.resize((img.size), resample=Image.BILINEAR)
        # empty_img = Image.new("RGBA", (img.size), 0)
        # img_out = Image.composite(img, empty_img, img_out.convert("L"))
        del d1, pred, predict, inputs_test, sample

        buffered = BytesIO()
        img_out.save(buffered, format="PNG")
        buffered.seek(0)
        img_byte = buffered.getvalue()
        img_str = "data:image/png;base64," + base64.b64encode(img_byte).decode()

    t = time.process_time_ns() - t
    print(t // 1e6, "ms")

    return img_str

# Serve React App
@app.route('/', defaults={'path': ''})
@app.route('/<path:path>')
def serve(path):
    if path != "" and os.path.exists(app.static_folder + '/' + path):
        return send_from_directory(app.static_folder, path)
    else:
        return send_from_directory(app.static_folder, 'index.html')

# main driver function
if __name__ == '__main__':
    # run() method of Flask class runs the application on the local development server.
    if torch.cuda.is_available():
        model_pred.load_state_dict(torch.load(model_path))
        model_pred.cuda()
    else:
        model_pred.load_state_dict(torch.load(model_path, map_location='cpu'))
    model_pred.eval()
    app.run(host='0.0.0.0', port=5000)


# normalize the predicted SOD probability map
def norm_pred(d):
    mi = torch.min(d)
    return (d - mi) / (torch.max(d) - mi)

class RescaleT(object):
    def __init__(self, output_size):
        assert isinstance(output_size, (int, ))
        self.output_size = output_size

    def __call__(self, sample):
        image = sample["image"]
        img = transform.resize(image, (self.output_size, self.output_size), mode='constant')
        return {"image": img}

class ToTensorLab(object):
    """Convert ndarrays in sample to Tensors."""
    def __init__(self,flag=0):
        self.flag = flag

    def __call__(self, sample):
        image=sample['image']
        # change the color space with rgb color
        tmpImg = np.zeros((image.shape[0],image.shape[1],3))
        image = image/np.max(image)
        if image.shape[2]==1:
            tmpImg[:,:,0] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,1] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,2] = (image[:,:,0]-0.485)/0.229
        else:
            tmpImg[:,:,0] = (image[:,:,0]-0.485)/0.229
            tmpImg[:,:,1] = (image[:,:,1]-0.456)/0.224
            tmpImg[:,:,2] = (image[:,:,2]-0.406)/0.225

        tmpImg = tmpImg.transpose((2, 0, 1))

        return {'image': torch.from_numpy(tmpImg)}


def preprocess_img(image):
    transform = transforms.Compose([RescaleT(320), ToTensorLab(flag=0)])
    sample = transform({"image": image})

    return sample

@app.route('/segmentation', methods=['POST'])
def process():

    t = time.process_time_ns()

    with torch.no_grad():
        # 22 = len("data:image/png;base64,")
        img = Image.open(BytesIO(base64.b64decode(request.form.get("base64", "")[22:])))

        sample = preprocess_img(np.array(img))
        inputs_test = torch.FloatTensor(sample["image"].unsqueeze(0).float())
        if torch.cuda.is_available():
            inputs_test = torch.autograd.Variable(inputs_test.cuda())
        else:
            inputs_test = torch.autograd.Variable(inputs_test)
        d1, _, _, _, _, _, _ = model_pred(inputs_test)
        pred = d1[:, 0, :, :]
        predict = norm_pred(pred).squeeze().cpu().detach().numpy()
        img_out = Image.fromarray(predict * 255).convert("RGB")
        img_out = img_out.resize((img.size), resample=Image.BILINEAR)
        # empty_img = Image.new("RGBA", (img.size), 0)
        # img_out = Image.composite(img, empty_img, img_out.convert("L"))
        del d1, pred, predict, inputs_test, sample

        buffered = BytesIO()
        img_out.save(buffered, format="PNG")
        buffered.seek(0)
        img_byte = buffered.getvalue()
        img_str = "data:image/png;base64," + base64.b64encode(img_byte).decode()

    t = time.process_time_ns() - t
    print(t // 1e6, "ms")

    return img_str

# Serve React App
@app.route('/', defaults={'path': ''})
@app.route('/<path:path>')
def serve(path):
    if path != "" and os.path.exists(app.static_folder + '/' + path):
        return send_from_directory(app.static_folder, path)
    else:
        return send_from_directory(app.static_folder, 'index.html')

# main driver function
if __name__ == '__main__':
    # run() method of Flask class runs the application on the local development server.
    if torch.cuda.is_available():
        model_pred.load_state_dict(torch.load(model_path))
        model_pred.cuda()
    else:
        model_pred.load_state_dict(torch.load(model_path, map_location='cpu'))
    model_pred.eval()
    app.run(host='0.0.0.0', port=5000)
